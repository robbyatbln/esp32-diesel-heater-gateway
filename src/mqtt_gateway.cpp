#include "mqtt_gateway.h"
#include <WiFi.h>
#include <Preferences.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <cmath>

extern String label;
extern void logEvent(const String& message);
namespace {
struct Config {
  bool enabled=false, discovery=true;
  String host, username, password;
  uint16_t port=1883, interval=30;
} config, pending;
struct Message {
  int type=0, id=0, error=0;
  bool retained=false, duplicate=false;
  char topic[160]={}, data[128]={};
};
QueueHandle_t inbox=nullptr;
std::atomic<uint32_t> dropped{0};
esp_mqtt_client_handle_t client=nullptr;
Preferences storage;
bool online=false, reconfigure=false, forcePublish=false;
String deviceId, base, availability, version, lastError, lastResult;
uint32_t sent=0, received=0, acknowledged=0, lastPublish=0, nextDiscovery=0;
uint32_t discoveryAt=0, discovered=0;
int discoveryIndex=-1, discoveryIds[12]={};
bool discoveryAck[12]={};
int changePhase=0,deleteIds[12]={},deleteCount=0,deleteAckCount=0,offlineId=-1;
bool deleteAck[12]={},offlineAck=false,cleanupFailed=false;
uint32_t changeDeadline=0;

struct Entity {const char* component;const char* key;const char* name;bool heater;};
constexpr Entity entities[]={
  {"sensor","wifi_signal","WLAN-Signal",false},
  {"sensor","uptime","Laufzeit",false},
  {"sensor","free_heap","Freier Speicher",false},
  {"binary_sensor","heater_connection","Heizungsverbindung",false},
  {"button","refresh","Status aktualisieren",false},
  {"button","restart","Gateway neu starten",false},
  {"climate","heater","Heizung",true},
  {"number","level","Leistungsstufe",true},
  {"select","operating_mode","Betriebsart",true},
  {"sensor","room_temperature","Raumtemperatur",true},
  {"sensor","voltage","Versorgungsspannung",true},
  {"sensor","case_temperature","Gehäusetemperatur",true}
};
constexpr int ENTITY_COUNT=sizeof(entities)/sizeof(entities[0]);
static_assert(ENTITY_COUNT==12,"Discovery acknowledgement slots must match entities");

String topic(const char* suffix) {return base+"/"+suffix;}
String discoveryTopic(int i) {return "homeassistant/"+String(entities[i].component)+"/"+deviceId+"/"+entities[i].key+"/config";}
bool flag(JsonVariantConst v) {return v.as<String>()=="true";}
bool integer(JsonVariantConst v,int min,int max,int& out) {
  String s=v.as<String>();if(s.isEmpty()||s.length()>5)return false;
  for(char c:s)if(c<'0'||c>'9')return false;
  out=s.toInt();return out>=min&&out<=max;
}
void saveConfig() {
  JsonDocument d;d["enabled"]=config.enabled;d["discovery"]=config.discovery;
  d["host"]=config.host;d["username"]=config.username;d["password"]=config.password;
  d["port"]=config.port;d["interval"]=config.interval;
  String data;serializeJson(d,data);storage.putString("config",data);
}

// ESP-MQTT invokes this on its own task. Only bounded copies cross into the main loop.
void onEvent(void*,esp_event_base_t,int32_t id,void* eventData) {
  auto event=static_cast<esp_mqtt_event_handle_t>(eventData);
  Message msg;msg.type=id;msg.id=event->msg_id;
  if(id==MQTT_EVENT_DATA) {
    if(event->current_data_offset!=0)return;
    if(event->topic_len<=0||event->topic_len>=int(sizeof(msg.topic))||event->total_data_len>=int(sizeof(msg.data))||event->total_data_len!=event->data_len){dropped++;return;}
    memcpy(msg.topic,event->topic,event->topic_len);
    if(event->data_len)memcpy(msg.data,event->data,event->data_len);
    // Reject embedded NUL bytes rather than parsing a truncated command.
    if(int(strlen(msg.data))!=event->data_len){dropped++;return;}
    msg.retained=event->retain;msg.duplicate=event->dup;
  } else if(id==MQTT_EVENT_ERROR) {
    if(event->error_handle&&event->error_handle->error_type==MQTT_ERROR_TYPE_CONNECTION_REFUSED)
      msg.error=event->error_handle->connect_return_code;
  } else if(id!=MQTT_EVENT_CONNECTED&&id!=MQTT_EVENT_DISCONNECTED&&id!=MQTT_EVENT_PUBLISHED)return;
  if(inbox&&xQueueSend(inbox,&msg,0)!=pdTRUE)dropped++;
}
int publish(const String& name,const String& payload,bool retain=true) {
  if(!client||!online)return -1;
  if(esp_mqtt_client_get_outbox_size(client)>24000){lastError="MQTT-Sendepuffer voll; Nachricht nicht gesendet.";return -1;}
  int id=esp_mqtt_client_enqueue(client,name.c_str(),payload.c_str(),payload.length(),1,retain,true);
  if(id>=0)sent++;else lastError="MQTT-Nachricht konnte nicht eingereiht werden.";
  return id;
}
void requestDiscovery() {
  if(!online||!config.discovery)return;
  discoveryIndex=0;discovered=0;discoveryAt=0;
  memset(discoveryIds,0,sizeof(discoveryIds));memset(discoveryAck,0,sizeof(discoveryAck));
  nextDiscovery=millis()+250+(esp_random()%1000);
}
void stopClient(bool sendOffline=true) {
  if(client) {
    if(online&&sendOffline)esp_mqtt_client_publish(client,availability.c_str(),"offline",0,1,true);
    esp_mqtt_client_stop(client);esp_mqtt_client_destroy(client);client=nullptr;
  }
  online=false;discoveryIndex=-1;if(inbox)xQueueReset(inbox);
}
void finishConfig() {
  stopClient(false);config=pending;saveConfig();reconfigure=false;changePhase=0;lastError="";
  discovered=0;discoveryAt=0;memset(discoveryIds,0,sizeof(discoveryIds));
  logEvent(config.enabled?"MQTT-Konfiguration gespeichert; Verbindung wird aufgebaut.":"MQTT deaktiviert.");
}
void advanceConfig() {
  if(!reconfigure)return;
  bool needsCleanup=config.discovery&&(!pending.discovery||config.host!=pending.host||config.port!=pending.port);
  uint32_t now=millis();
  if(changePhase==0) {discoveryIndex=-1;changePhase=1;changeDeadline=now+3000;}
  if(!online||!client) {
    if(needsCleanup&&config.enabled)logEvent("Alter MQTT-Broker offline: bisherige HA-Einträge ggf. manuell entfernen.");
    finishConfig();return;
  }
  if(changePhase==1) {
    if(esp_mqtt_client_get_outbox_size(client)>0&&int32_t(now-changeDeadline)<0)return;
    deleteCount=0;deleteAckCount=0;cleanupFailed=false;memset(deleteAck,0,sizeof(deleteAck));memset(deleteIds,0,sizeof(deleteIds));
    if(needsCleanup)for(int i=0;i<ENTITY_COUNT;i++) {
      int id=publish(discoveryTopic(i),"");deleteIds[i]=id;
      if(id>=0)deleteCount++;else cleanupFailed=true;
    }
    changePhase=2;changeDeadline=now+4000;return;
  }
  if(changePhase==2) {
    if(deleteAckCount<deleteCount&&int32_t(now-changeDeadline)<0)return;
    if(needsCleanup)logEvent(!cleanupFailed&&deleteAckCount==ENTITY_COUNT?"Home-Assistant-Einträge entfernt; Broker hat bestätigt.":"Entfernen alter HA-Einträge nicht vollständig bestätigt.");
    offlineAck=false;offlineId=publish(availability,"offline");changePhase=3;changeDeadline=now+2000;return;
  }
  if(changePhase==3&&(offlineAck||int32_t(now-changeDeadline)>=0))finishConfig();
}
void startClient() {
  if(client||!config.enabled||WiFi.status()!=WL_CONNECTED)return;
  esp_mqtt_client_config_t c={};
  c.host=config.host.c_str();c.port=config.port;c.client_id=deviceId.c_str();
  c.username=config.username.isEmpty()?nullptr:config.username.c_str();
  c.password=config.password.isEmpty()?nullptr:config.password.c_str();
  c.transport=MQTT_TRANSPORT_OVER_TCP;c.protocol_ver=MQTT_PROTOCOL_V_3_1_1;
  c.lwt_topic=availability.c_str();c.lwt_msg="offline";c.lwt_qos=1;c.lwt_retain=true;
  c.keepalive=20;c.reconnect_timeout_ms=10000;c.network_timeout_ms=1500;
  c.buffer_size=2048;c.out_buffer_size=2048;c.task_stack=6144;
  client=esp_mqtt_client_init(&c);
  if(!client){lastError="MQTT-Client konnte nicht gestartet werden.";return;}
  esp_mqtt_client_register_event(client,static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),onEvent,nullptr);
  if(esp_mqtt_client_start(client)!=ESP_OK){esp_mqtt_client_destroy(client);client=nullptr;lastError="MQTT-Start fehlgeschlagen.";}
}
void commonDiscovery(JsonDocument& d,int i) {
  auto& e=entities[i];d["name"]=e.name;d["unique_id"]=deviceId+"_"+e.key;
  auto dev=d["device"].to<JsonObject>();dev["identifiers"].to<JsonArray>().add(deviceId);
  dev["name"]=label;dev["manufacturer"]="DIY";dev["model"]="Diesel Heater Gateway · XIAO ESP32-S3";
  dev["sw_version"]=version;dev["configuration_url"]="http://"+WiFi.localIP().toString()+"/#mqtt";
  auto origin=d["origin"].to<JsonObject>();origin["name"]="Diesel Heater Gateway";origin["sw_version"]=version;
  auto av=d["availability"].to<JsonArray>();av.add<JsonObject>()["topic"]=availability;
  if(e.heater)av.add<JsonObject>()["topic"]=topic("heater/availability");
  d["availability_mode"]="all";d["qos"]=1;
  if(!e.heater)d["entity_category"]="diagnostic";
}
void sendDiscovery(int i) {
  JsonDocument d;commonDiscovery(d,i);String key=entities[i].key;
  String gatewayState=topic("state"),heaterState=topic("heater/state");
  if(i==0||i==1||i==2||i>=9) {
    d["state_topic"]=entities[i].heater?heaterState:gatewayState;
    d["value_template"]="{{ value_json."+key+" }}";
    d["state_class"]="measurement";
    if(i==0){d["device_class"]="signal_strength";d["unit_of_measurement"]="dBm";}
    if(i==1){d["device_class"]="duration";d["unit_of_measurement"]="s";}
    if(i==2){d["device_class"]="data_size";d["unit_of_measurement"]="B";}
    if(i==9||i==11){d["device_class"]="temperature";d["unit_of_measurement"]="°C";}
    if(i==10){d["device_class"]="voltage";d["unit_of_measurement"]="V";}
  } else if(i==3) {
    d["state_topic"]=gatewayState;d["device_class"]="connectivity";
    d["value_template"]="{{ 'ON' if value_json.heater_connected else 'OFF' }}";
  } else if(i==4||i==5) {
    d["command_topic"]=topic(("command/"+key).c_str());d["payload_press"]="PRESS";d["retain"]=false;
    if(i==5)d["device_class"]="restart";else d["icon"]="mdi:refresh";
  } else if(i==6) {
    d["modes"].to<JsonArray>().add("off");d["modes"].as<JsonArray>().add("heat");
    if(gatewaySupportsVentilation()) d["modes"].as<JsonArray>().add("fan_only");
    d["mode_command_topic"]=topic("command/mode");d["mode_state_topic"]=heaterState;d["mode_state_template"]="{{ value_json.mode }}";
    d["power_command_topic"]=topic("command/power");
    d["temperature_command_topic"]=topic("command/temperature");d["temperature_state_topic"]=heaterState;d["temperature_state_template"]="{{ value_json.target_temperature }}";
    d["current_temperature_topic"]=heaterState;d["current_temperature_template"]="{{ value_json.room_temperature }}";
    d["action_topic"]=heaterState;d["action_template"]="{{ value_json.action }}";
    d["min_temp"]=8;d["max_temp"]=35;d["temp_step"]=1;d["precision"]=0.1;d["temperature_unit"]="C";d["optimistic"]=false;d["retain"]=false;
  } else if(i==7) {
    d["command_topic"]=topic("command/level");d["state_topic"]=heaterState;d["value_template"]="{{ value_json.level }}";
    d["min"]=1;d["max"]=10;d["step"]=1;d["mode"]="slider";d["optimistic"]=false;d["retain"]=false;
  } else if(i==8) {
    d["command_topic"]=topic("command/operating_mode");d["state_topic"]=heaterState;d["value_template"]="{{ value_json.operating_mode }}";
    auto options=d["options"].to<JsonArray>();options.add("temperature");options.add("level");if(gatewaySupportsVentilation())options.add("ventilation");d["optimistic"]=false;d["retain"]=false;
  }
  String data;serializeJson(d,data);int id=publish(discoveryTopic(i),data);
  if(id>=0)discoveryIds[i]=id;
}
void result(const String& command,bool ok,const String& error) {
  JsonDocument d;d["command"]=command;d["ok"]=ok;d["error"]=error;d["uptime"]=millis()/1000;
  String data;serializeJson(d,data);publish(topic("result"),data,false);
  lastResult=command+": "+(ok?String("ausgeführt"):error);
  logEvent("MQTT-Befehl "+lastResult);
}
bool numeric(const String& text,float min,float max,bool whole) {
  if(text.isEmpty()||text.length()>12)return false;
  char* end=nullptr;float v=strtof(text.c_str(),&end);
  return end&&*end=='\0'&&std::isfinite(v)&&v>=min&&v<=max&&(!whole||floorf(v)==v);
}
void handleCommand(const Message& msg) {
  String name=msg.topic,value=msg.data;
  if(name=="homeassistant/status") {if(value=="online"){requestDiscovery();forcePublish=true;}return;}
  String prefix=topic("command/");if(!name.startsWith(prefix))return;
  received++;String command=name.substring(prefix.length());value.trim();
  if(msg.retained){if(value.length())result(command,false,"retained_command_rejected");return;}
  if(msg.duplicate){result(command,false,"duplicate_command_ignored");return;}
  bool valid=false,service=false;
  if(command=="refresh"||command=="restart"||command=="scan"){valid=value=="PRESS";service=true;}
  else if(command=="power")valid=value=="ON"||value=="OFF";
  else if(command=="mode")valid=value=="heat"||value=="off"||value=="fan_only";
  else if(command=="operating_mode")valid=value=="temperature"||value=="level"||value=="ventilation";
  else if(command=="temperature")valid=numeric(value,8,35,true);
  else if(command=="level")valid=numeric(value,1,10,true);
  else {result(command,false,"unknown_command");return;}
  if(!valid){result(command,false,"invalid_payload");return;}
  if(command=="refresh"){forcePublish=true;result(command,true,"");return;}
  String error;bool ok=service?gatewayServiceCommand(command,error):gatewayHeaterCommand(command,value,error);
  result(command,ok,error);
}
}

void mqttSetup(const char* firmwareVersion) {
  version=firmwareVersion;String mac=WiFi.macAddress();mac.replace(":","");mac.toLowerCase();
  deviceId="dieselheater_"+mac;base="dieselheater/"+mac;availability=topic("availability");
  inbox=xQueueCreate(20,sizeof(Message));storage.begin("mqtt",false);
  JsonDocument d;if(!deserializeJson(d,storage.getString("config","{}"))) {
    config.enabled=d["enabled"]|false;config.discovery=d["discovery"]|true;
    config.host=d["host"]|"";config.username=d["username"]|"";config.password=d["password"]|"";
    config.port=d["port"]|1883;config.interval=d["interval"]|30;
  }
  if(!inbox){config.enabled=false;lastError="MQTT-Empfangspuffer nicht verfügbar.";}
}
void mqttInfo(JsonObject out,bool includeConfig) {
  out["enabled"]=config.enabled;out["connected"]=online;out["discovery"]=config.discovery;
  out["deviceId"]=deviceId;out["baseTopic"]=base;out["sent"]=sent;out["received"]=received;
  out["acknowledged"]=acknowledged;out["dropped"]=dropped.load();out["lastError"]=lastError;out["lastResult"]=lastResult;
  out["discoveryAcknowledged"]=discovered;out["discoveryTotal"]=ENTITY_COUNT;out["discoveryAt"]=discoveryAt;
  out["changing"]=reconfigure;
  if(includeConfig) {out["host"]=config.host;out["port"]=config.port;out["username"]=config.username;out["passwordSet"]=!config.password.isEmpty();out["interval"]=config.interval;}
}
bool mqttConfigure(JsonObjectConst fields,String& error) {
  if(reconfigure){error="MQTT-Konfiguration wird gerade übernommen.";return false;}
  Config c;c.enabled=flag(fields["enabled"]);c.discovery=flag(fields["discovery"]);
  c.host=fields["host"].as<String>();c.host.trim();c.username=fields["username"].as<String>();
  c.password=flag(fields["keepPassword"])?config.password:fields["password"].as<String>();
  int port,interval;
  if(!integer(fields["port"],1,65535,port)||!integer(fields["interval"],10,300,interval)){error="Port: 1–65535. Sendeintervall: 10–300 Sekunden.";return false;}
  c.port=port;c.interval=interval;
  if(c.host.length()>128||(c.enabled&&c.host.isEmpty())||c.username.length()>64||c.password.length()>128){error="Broker-Adresse oder Zugangsdaten fehlen bzw. sind zu lang.";return false;}
  for(char ch:c.host)if(!isalnum(static_cast<unsigned char>(ch))&&ch!='.'&&ch!='-'&&ch!='_'){error="Broker als IPv4-Adresse oder Hostname ohne mqtt:// und ohne Port eingeben.";return false;}
  if(flag(fields["keepPassword"])&&!config.password.isEmpty()&&(c.host!=config.host||c.port!=config.port||c.username!=config.username)) {
    error="Bei geändertem Broker oder Benutzer das Passwort neu eingeben.";return false;
  }
  if(c.password.length()&&c.username.isEmpty()){error="Für ein Passwort bitte auch einen Benutzernamen angeben.";return false;}
  pending=c;reconfigure=true;return true;
}
bool mqttRediscover() {if(!online||!config.discovery||reconfigure)return false;requestDiscovery();return true;}
bool mqttPublishStatus() {
  if(!online||reconfigure)return false;
  JsonDocument d;d["version"]=version;d["uptime"]=millis()/1000;d["wifi_signal"]=WiFi.RSSI();d["free_heap"]=ESP.getFreeHeap();
  d["heater_connected"]=false;d["driver_ready"]=false;
  String data;serializeJson(d,data);bool ok=publish(topic("state"),data)>=0;
  publish(topic("heater/availability"),"offline");lastPublish=millis();return ok;
}
void mqttLabelChanged() {requestDiscovery();forcePublish=true;}
void mqttShutdown() {stopClient();}
void mqttLoop() {
  if(!reconfigure)startClient();
  Message msg;
  for(int i=0;inbox&&i<20&&xQueueReceive(inbox,&msg,0)==pdTRUE;i++) {
    if(msg.type==MQTT_EVENT_CONNECTED) {
      online=true;lastError="";logEvent("MQTT mit Broker verbunden.");
      if(!reconfigure) {
        esp_mqtt_client_subscribe(client,topic("command/+").c_str(),1);
        esp_mqtt_client_subscribe(client,"homeassistant/status",1);
        publish(availability,"online");forcePublish=true;requestDiscovery();
      }
    } else if(msg.type==MQTT_EVENT_DISCONNECTED) {if(online)logEvent("MQTT-Verbindung getrennt; Wiederverbindung läuft.");online=false;discoveryIndex=-1;}
    else if(msg.type==MQTT_EVENT_ERROR) {
      String error=msg.error==4||msg.error==5?"Broker lehnt Benutzername/Passwort oder Berechtigung ab.":"Broker nicht erreichbar oder Verbindung abgelehnt.";
      if(lastError!=error){lastError=error;logEvent("MQTT: "+error);}
    } else if(msg.type==MQTT_EVENT_PUBLISHED) {
      acknowledged++;
      if(reconfigure) {
        for(int j=0;j<ENTITY_COUNT;j++)if(deleteIds[j]==msg.id&&!deleteAck[j]){deleteAck[j]=true;deleteAckCount++;}
        if(msg.id==offlineId)offlineAck=true;
      }
      for(int j=0;j<ENTITY_COUNT;j++)if(discoveryIds[j]==msg.id&&!discoveryAck[j]){discoveryAck[j]=true;discovered++;if(discovered==ENTITY_COUNT){discoveryAt=millis()/1000;logEvent("Home-Assistant-Discovery: 12 Einträge vom Broker bestätigt.");}}
    } else if(msg.type==MQTT_EVENT_DATA&&!reconfigure)handleCommand(msg);
  }
  if(reconfigure){advanceConfig();return;}
  if(online&&(forcePublish||millis()-lastPublish>=uint32_t(config.interval)*1000)){forcePublish=false;mqttPublishStatus();}
  if(online&&discoveryIndex>=0&&int32_t(millis()-nextDiscovery)>=0) {
    sendDiscovery(discoveryIndex++);nextDiscovery=millis()+100;
    if(discoveryIndex>=ENTITY_COUNT){discoveryIndex=-1;forcePublish=true;}
  }
}
