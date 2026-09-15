#include "heater_gateway.h"
#include "heater_protocol.h"
#include "heater_control.h"
#include "firmware_update.h"
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <esp_timer.h>
#include <time.h>
#include <math.h>

extern void logEvent(const String& message);
namespace {
using namespace heater;
struct Request {
  int kind=0, profile=0, argument=0;
  Command command=Command::Query;
  uint16_t pin=0;
  uint8_t addressType=0;
  char address[18]={};
  int64_t localEpoch=0, clockAt=0;
};
struct Snapshot {
  bool connecting=false, linked=false, valid=false, pending=false;
  State state;
  uint32_t receivedAt=0, packets=0, rejected=0, revision=0;
  int profile=-1;
  char status[100]="Nicht verbunden", result[64]="";
};
struct Rx {uint8_t data[64];size_t size;uint32_t at;};
QueueHandle_t requests=nullptr, snapshots=nullptr, notifications=nullptr;
std::atomic<bool> stopRequested{false};
std::atomic<bool> linkSession{false}, commandOutstanding{false};
std::atomic<uint32_t> droppedRx{0};
Snapshot visible;
bool requestActive=false;
uint32_t revision=0;
Preferences settings;
int profile=-1;
uint16_t pin=1234;
bool pinSet=false;

// Worker-only data: no Arduino String, Preferences, WebServer or MQTT calls.
NimBLEClient* client=nullptr;
NimBLERemoteCharacteristic* writer=nullptr;
Snapshot live;
Request connection, pending;
uint32_t nextPoll=0, pendingAt=0, connectedAt=0;
bool authenticated=false, handshakeSent=false;
uint8_t assembly[64]={};size_t assembled=0;uint32_t assemblyAt=0;
void push(){live.revision++;xQueueOverwrite(snapshots,&live);}
void status(const char* s){snprintf(live.status,sizeof(live.status),"%s",s);push();}
bool fresh(){return live.linked&&live.valid&&millis()-live.receivedAt<15000;}
Context context(){
  Context c;c.pin=connection.pin;c.state=fresh()?&live.state:nullptr;
  c.fahrenheit=live.state.unit==1;
  int j=0;for(char a:connection.address)if(a&&a!=':'&&j<12)c.mac[j++]=a;
  if(connection.localEpoch){
    time_t now=connection.localEpoch+(esp_timer_get_time()-connection.clockAt)/1000000;
    struct tm t;gmtime_r(&now,&t);c.clockValid=true;c.hour=t.tm_hour;c.minute=t.tm_min;c.second=t.tm_sec;c.weekday=t.tm_wday?t.tm_wday:7;
  }
  return c;
}
const char* errorFor(Result r){
  switch(r){case Result::InvalidArgument:return "invalid_value";case Result::Unsupported:return "command_not_supported";
    case Result::NeedState:return "fresh_matching_state_required";case Result::NeedClock:return "clock_required";
    default:return "protocol_error";}
}
bool send(Command cmd,int arg,const char*& error){
  Packet p;auto r=encode(Profile(connection.profile),cmd,arg,context(),p);
  if(r==Result::NoChange){error="already_in_requested_state";return true;}
  if(r!=Result::Ok){error=errorFor(r);return false;}
  if(!client||!client->isConnected()||!writer||stopRequested){error="heater_disconnected";return false;}
  if(!writer->writeValue(p.bytes,p.size,!writer->canWriteNoResponse())){error="ble_write_failed";return false;}
  return true;
}
void notification(NimBLERemoteCharacteristic*,uint8_t* data,size_t size,bool){
  if(!size||size>64){droppedRx++;return;}
  Rx r;r.size=size;r.at=millis();memcpy(r.data,data,size);
  if(xQueueSend(notifications,&r,0)!=pdTRUE)droppedRx++;
}
void closeLink(const char* reason){
  writer=nullptr;
  if(client){client->disconnect();NimBLEDevice::deleteClient(client);client=nullptr;}
  live.connecting=false;live.linked=false;live.valid=false;
  if(live.pending)snprintf(live.result,sizeof(live.result),"connection_lost_not_confirmed");
  live.pending=false;authenticated=false;handshakeSent=false;assembled=0;
  xQueueReset(notifications);linkSession=false;commandOutstanding=false;status(reason);
}
bool characteristics(){
  uint16_t serviceId=connection.profile==7?0xbd39:connection.profile==4||connection.profile==6?0xfff0:0xffe0;
  auto svc=client->getService(NimBLEUUID(serviceId));
  if(!svc&&connection.profile==5){serviceId=0xfff0;svc=client->getService(NimBLEUUID(serviceId));}
  if(!svc)return false;
  auto notify=svc->getCharacteristic(NimBLEUUID(uint16_t(serviceId==0xbd39?0xbdf8:serviceId==0xfff0?0xfff1:0xffe1)));
  if(serviceId==0xbd39)writer=svc->getCharacteristic(NimBLEUUID(uint16_t(0xbdf7)));
  else if(connection.profile==4||connection.profile==6)writer=svc->getCharacteristic(NimBLEUUID(uint16_t(0xfff2)));
  else{
    writer=notify;
    if(writer&&!writer->canWrite()&&!writer->canWriteNoResponse())writer=svc->getCharacteristic(NimBLEUUID(uint16_t(serviceId==0xfff0?0xfff2:0xffe2)));
    if(notify&&!notify->canNotify()&&!notify->canIndicate())notify=svc->getCharacteristic(NimBLEUUID(uint16_t(serviceId==0xfff0?0xfff2:0xffe2)));
  }
  return notify&&writer&&(writer->canWrite()||writer->canWriteNoResponse())&&(notify->canNotify()||notify->canIndicate())&&notify->subscribe(notify->canNotify(),notification);
}
void connectLink(const Request& r){
  connection=r;live=Snapshot{};live.profile=r.profile;live.connecting=true;status("Bluetooth verbindet …");
  client=NimBLEDevice::createClient();
  if(!client){closeLink("Bluetooth-Client nicht verfügbar");return;}
  client->setConnectTimeout(7000);
  if(!client->connect(NimBLEAddress(std::string(r.address),r.addressType))||stopRequested){closeLink("Heizung nicht erreichbar");return;}
  if(!characteristics()||stopRequested){closeLink("Passende Bluetooth-Schnittstelle fehlt");return;}
  live.connecting=false;live.linked=true;connectedAt=millis();nextPoll=0;
  authenticated=r.profile!=7;
  if(r.profile==7){
    const char* error="";
    if(!send(Command::Authenticate,0,error)){closeLink(error);return;}
    handshakeSent=true;status("PIN-Anmeldung läuft …");
  }else status("Verbunden · warte auf Status");
}
void receivePacket(const Rx& r){
  if(connection.profile==7&&r.size>=17&&r.data[0]==0&&r.data[1]==3&&r.data[7]==0x0b&&r.data[8]==0x0c){
    if(!handshakeSent||authenticated)return;
    if(r.data[16]!=1){closeLink("PIN abgelehnt");return;}
    authenticated=true;nextPoll=0;status("PIN bestätigt · warte auf Status");return;
  }
  if(connection.profile==5&&r.size>=2&&r.data[0]==0xaa&&r.data[1]==0x77){
    if(!handshakeSent){const char* error="";if(!send(Command::Authenticate,0,error)){closeLink(error);return;}handshakeSent=true;}
    return;
  }
  if(!authenticated)return;
  size_t minimum=connection.profile==0?18:connection.profile==1||connection.profile==3?48:connection.profile==2?20:connection.profile==4?21:connection.profile==5?46:38;
  if(assembled&&r.at-assemblyAt>500)assembled=0;
  if(assembled+r.size>64){assembled=0;live.rejected++;return;}
  memcpy(assembly+assembled,r.data,r.size);assembled+=r.size;assemblyAt=r.at;
  if(assembled<minimum)return;
  State s;auto ctx=context();auto result=parse(Profile(connection.profile),assembly,assembled,s,ctx.mac);assembled=0;
  // Fresh means a plausible, recognised full status, not just a BLE connection.
  if(result!=Result::Ok||s.voltage<0||s.voltage>100||fabsf(s.cabin)>500||s.mode<0||s.mode>3||s.unit>1){live.rejected++;return;}
  live.state=s;live.valid=true;live.receivedAt=r.at;live.packets++;
  if(live.pending&&confirms(s,connection.profile,pending.command,pending.argument,r.at,pendingAt)){
    live.pending=false;commandOutstanding=false;snprintf(live.result,sizeof(live.result),"confirmed");
  }
  status(s.error?"Heizung meldet einen Fehler":"Live-Status empfangen");
}
void execute(const Request& r){
  const char* error=controlError(live.state,connection.profile,fresh(),live.pending,r.command,r.argument);
  if(*error){snprintf(live.result,sizeof(live.result),"%s",error);live.pending=false;commandOutstanding=false;push();return;}
  Request actual=r;
  if(connection.profile>=6&&live.state.unit==1&&r.command==Command::Temperature)actual.argument=lroundf(r.argument*9.f/5+32);
  if(!send(actual.command,actual.argument,error)){snprintf(live.result,sizeof(live.result),"%s",error);live.pending=false;commandOutstanding=false;push();return;}
  if(*error){snprintf(live.result,sizeof(live.result),"%s",error);commandOutstanding=false;push();return;}
  pending=actual;pendingAt=millis();live.pending=true;nextPoll=pendingAt+500;
  snprintf(live.result,sizeof(live.result),"sent_awaiting_confirmation");push();
}
void worker(void*){
  for(;;){
    if(stopRequested){closeLink("Verbindung getrennt");xQueueReset(requests);stopRequested=false;}
    Request r;
    if(xQueueReceive(requests,&r,pdMS_TO_TICKS(20))==pdTRUE){if(r.kind==1)connectLink(r);else execute(r);}
    if(live.linked){
      if(!client||!client->isConnected()){closeLink("Bluetooth-Verbindung verloren");continue;}
      Rx rx;for(int i=0;i<12&&xQueueReceive(notifications,&rx,0)==pdTRUE;i++){receivePacket(rx);if(!live.linked)break;}
      if(!live.linked)continue;
      uint32_t now=millis();
      if(!authenticated&&now-connectedAt>5000){closeLink("PIN-Anmeldung nicht bestätigt");continue;}
      if(!live.valid&&now-connectedAt>20000){closeLink("Kein gültiger Status · Profil und PIN prüfen");continue;}
      if(live.valid&&now-live.receivedAt>15000){live.valid=false;status("Status veraltet · Steuerung gesperrt");}
      if(live.pending&&now-pendingAt>10000){live.pending=false;commandOutstanding=false;snprintf(live.result,sizeof(live.result),"not_confirmed_no_retry");push();}
      if(authenticated&&int32_t(now-nextPoll)>=0){
        nextPoll=now+3000;const char* error="";if(!send(Command::Query,0,error)){closeLink(error);continue;}
      }
    }
  }
}
bool decimal(const String& s,long min,long max,long& value){
  if(s.isEmpty()||s.length()>10)return false;
  for(char c:s)if(c<'0'||c>'9')return false;
  char* end;long long n=strtoll(s.c_str(),&end,10);if(*end||n<min||n>max)return false;value=n;return true;
}
float celsius(float n){return visible.state.unit==1?(n-32)*5.f/9:n;}
}
void heaterSetup(){
  settings.begin("heater",false);profile=settings.getInt("profile",-1);if(profile<0||profile>7)profile=-1;
  pin=settings.getUShort("pin",1234);pinSet=settings.getBool("pinSet",false);
  requests=xQueueCreate(1,sizeof(Request));snapshots=xQueueCreate(1,sizeof(Snapshot));notifications=xQueueCreate(12,sizeof(Rx));
  if(!requests||!snapshots||!notifications||xTaskCreate(worker,"heater-ble",8192,nullptr,1,nullptr)!=pdPASS){
    snprintf(visible.status,sizeof(visible.status),"Bluetooth-Arbeitsspeicher fehlt");requests=nullptr;
  }
}
void heaterLoop(){
  Snapshot s;
  if(snapshots&&xQueueReceive(snapshots,&s,0)==pdTRUE){
    bool changed=strcmp(s.status,visible.status)!=0;
    if(s.result[0]&&strcmp(s.result,visible.result)!=0)logEvent(String("Heizungsbefehl: ")+s.result);
    visible=s;requestActive=false;revision++;
    if(changed)logEvent(String("Heizung: ")+s.status);
  }
}
bool heaterAvailable(){return linkSession&&!stopRequested&&visible.linked&&visible.valid&&millis()-visible.receivedAt<15000;}
bool heaterControlsReady(){return heaterAvailable()&&!commandOutstanding&&!stopRequested&&!firmwareUpdateBusy();}
bool heaterRadioBusy(){return linkSession||stopRequested;}
uint32_t heaterRevision(){return revision;}
bool heaterVentilationSupported(){return profile==4;}
void heaterResetConfiguration(){
  if(heaterRadioBusy())return;
  settings.clear();profile=-1;pin=1234;pinSet=false;revision++;
}
bool heaterConfigure(const String& selection,const String& value,bool keep,String& error){
  if(heaterRadioBusy()){error="Bitte zuerst die Bluetooth-Verbindung trennen.";return false;}
  long n,p;if(!decimal(selection,0,7,n)||(!keep&&!decimal(value,0,9999,p))||(!keep&&value.length()!=4)){
    error="Protokoll auswählen und eine vierstellige PIN eingeben.";return false;
  }
  if(keep&&!pinSet){error="Bitte zuerst die Geräte-PIN eingeben.";return false;}
  profile=n;if(!keep)pin=p;pinSet=true;
  settings.putInt("profile",profile);settings.putUShort("pin",pin);settings.putBool("pinSet",true);revision++;return true;
}
bool heaterConnect(const String& address,uint8_t addressType,const String& epoch,String& error){
  if(!requests){error="Bluetooth-Treiber nicht bereit.";return false;}
  if(heaterRadioBusy()){error="Bluetooth ist beschäftigt.";return false;}
  if(profile<0||!pinSet){error="Bitte Protokoll und PIN speichern.";return false;}
  if(address.length()!=17){error="Bitte zuerst eine Heizung auswählen.";return false;}
  for(int i=0;i<17;i++)if(i%3==2?address[i]!=':':!isxdigit(static_cast<unsigned char>(address[i]))){error="Ungültige Bluetooth-Adresse.";return false;}
  long clock=0;if(!epoch.isEmpty()&&!decimal(epoch,1700000000,2147483647,clock)){error="Browser-Uhrzeit ungültig.";return false;}
  if(profile==7&&!clock){error="MVP2 benötigt die Uhrzeit aus deinem Browser.";return false;}
  Request r;r.kind=1;r.profile=profile;r.pin=pin;r.addressType=addressType;r.localEpoch=clock;r.clockAt=esp_timer_get_time();strlcpy(r.address,address.c_str(),sizeof(r.address));
  linkSession=true;
  if(xQueueSend(requests,&r,0)!=pdTRUE){linkSession=false;error="Bluetooth ist beschäftigt.";return false;}
  requestActive=true;visible.connecting=true;revision++;return true;
}
void heaterDisconnect(){if(!requests)return;stopRequested=true;visible.valid=false;revision++;}
bool heaterCommand(const String& command,const String& value,String& error){
  if(!heaterControlsReady()){error=visible.pending||requestActive?"command_pending":"heater_state_unavailable";return false;}
  Request r;r.kind=2;long n;
  if(command=="power"&&(value=="ON"||value=="OFF")){r.command=Command::Power;r.argument=value=="ON";}
  else if(command=="mode"&&(value=="off"||value=="heat")){r.command=Command::Power;r.argument=value=="heat";}
  else if((command=="mode"&&value=="fan_only")||(command=="operating_mode"&&value=="ventilation")){r.command=Command::Mode;r.argument=3;}
  else if(command=="operating_mode"&&(value=="temperature"||value=="level")){r.command=Command::Mode;r.argument=value=="temperature"?2:1;}
  else if(command=="temperature"&&decimal(value,8,35,n)){r.command=Command::Temperature;r.argument=n;}
  else if(command=="level"&&decimal(value,1,10,n)){r.command=Command::Level;r.argument=n;}
  else{error="invalid_command";return false;}
  if(r.command==Command::Mode&&r.argument==3&&!heaterVentilationSupported()){error="ventilation_not_supported";return false;}
  commandOutstanding=true;
  if(!requests||xQueueSend(requests,&r,0)!=pdTRUE){commandOutstanding=false;error="command_pending";return false;}
  requestActive=true;visible.pending=true;strlcpy(visible.result,"queued",sizeof(visible.result));revision++;return true;
}
void heaterMqttState(JsonObject out){
  if(!heaterAvailable())return;
  const auto& s=visible.state;bool fan=fanState(s,profile),off=offState(s,profile),cool=cooldownState(s,profile);
  out["mode"]=fan?"fan_only":off||cool?"off":"heat";
  out["action"]=fan?"fan":off?"off":cool?"idle":s.step==3?"heating":"idle";
  out["operating_mode"]=fan?"ventilation":s.mode==2?"temperature":s.mode==1?"level":"unknown";
  out["room_temperature"]=celsius(s.cabin);out["case_temperature"]=celsius(s.body);out["voltage"]=s.voltage;
  if(s.hasTarget)out["target_temperature"]=profile==3?s.target:celsius(s.target);else out["target_temperature"]=nullptr;
  if(s.hasLevel)out["level"]=s.level;else out["level"]=nullptr;
  out["error_code"]=s.error;out["phase"]=s.step;out["cooldown"]=cool;out["pending"]=visible.pending||requestActive;
  out["command_result"]=visible.result;
}
void heaterInfo(JsonObject out){
  out["profile"]=profile;out["profileName"]=profile<0?"Bitte auswählen":name(Profile(profile));out["pinSet"]=pinSet;
  out["connecting"]=visible.connecting;out["linked"]=visible.linked;out["available"]=heaterAvailable();out["controlsReady"]=heaterControlsReady();
  out["pending"]=visible.pending||requestActive;out["status"]=visible.status;out["result"]=visible.result;out["revision"]=revision;
  out["age"]=visible.packets?(millis()-visible.receivedAt)/1000:0;out["packets"]=visible.packets;out["rejected"]=visible.rejected+droppedRx.load();
  out["ventilationSupported"]=heaterVentilationSupported();heaterMqttState(out["state"].to<JsonObject>());
}
