#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <atomic>
#include "web_ui.h"
#include "mqtt_gateway.h"
#include "firmware_update.h"

constexpr char VERSION[] = "0.4.0";
WebServer web(80);
DNSServer dns;
Preferences prefs;
NimBLEScan* scanner;
std::atomic<bool> scanDone{false};
bool scanning=false, wifiScanning=false, ap=false, connected=false;
bool hasScan=false, wifiHasScan=false;
uint32_t disconnectedAt=0, connectedAt=0, rebootAt=0, lastScan=0;
uint32_t scanRevision=0, wifiRevision=0, logSequence=0;
int deviceCount=0;
String ssid, selected, selectedName, selectedHint, token, label;
String devices="[]", networks="[]", serialLine;
struct Event { uint32_t at; String message; } events[24];
size_t eventNext=0, eventCount=0;

void logEvent(const String& message) {
  events[eventNext]={millis()/1000,message};
  eventNext=(eventNext+1)%24;
  if(eventCount<24) eventCount++;
  logSequence++;
  Serial.println(message);
}
void sendJson(JsonDocument& doc, int code=200) {
  String out;
  serializeJson(doc,out);
  web.sendHeader("Cache-Control","no-store");
  web.send(code,"application/json; charset=utf-8",out);
}
void success(int code=200) { web.send(code,"application/json","{\"ok\":true}"); }
bool allowed() {
  if(web.header("X-Gateway-Token")==token) {
    if(firmwareUpdateBusy()){web.send(409,"text/plain; charset=utf-8","Firmware-Update läuft. Bitte warten.");return false;}
    return true;
  }
  web.send(403,"text/plain; charset=utf-8","Sitzung abgelaufen. Bitte Seite neu laden.");
  return false;
}
bool radioBusy() {
  if(!scanning&&!wifiScanning) return false;
  web.send(409,"text/plain; charset=utf-8","Bitte die laufende Suche abwarten.");
  return true;
}
void startAP() {
  if(ap) return;
  WiFi.mode(WIFI_AP_STA);
  if(!WiFi.softAP("DieselHeater-Setup","dieselheater")) {
    logEvent("Setup-Hotspot konnte nicht gestartet werden.");disconnectedAt=millis();return;
  }
  dns.start(53,"*",WiFi.softAPIP());ap=true;
  logEvent("Setup-Hotspot aktiv: http://192.168.4.1");
}
class ScanCallbacks:public NimBLEScanCallbacks {
  void onScanEnd(const NimBLEScanResults&, int) override { scanDone=true; }
} scanCallbacks;
bool beginScan() {
  if(scanning||wifiScanning||firmwareUpdateBusy()) return false;
  scanner->clearResults();scanning=true;scanDone=false;
  if(!scanner->start(8000,false,true)) { scanning=false; return false; }
  logEvent("Bluetooth-Suche gestartet.");return true;
}
bool gatewaySupportsVentilation() { return false; } // Enable only in a verified heater driver.
bool gatewayHeaterCommand(const String& command,const String& value,String& error) {
  if(firmwareUpdateBusy()){error="update_in_progress";return false;}
  if ((command=="mode" && value=="fan_only") || (command=="operating_mode" && value=="ventilation")) {
    if (!gatewaySupportsVentilation()) { error="ventilation_not_supported"; return false; }
  }
  // Replace this only after a BLE protocol driver can confirm actual state changes.
  error="heater_driver_unavailable";
  return false;
}
bool gatewayServiceCommand(const String& command,String& error) {
  if(firmwareUpdateBusy()){error="update_in_progress";return false;}
  if(scanning||wifiScanning){error="radio_busy";return false;}
  if(command=="restart"){rebootAt=millis()+1500;return true;}
  if(command=="scan"&&beginScan())return true;
  error="command_unavailable";return false;
}
void finishScan() {
  JsonDocument doc;auto arr=doc.to<JsonArray>();auto results=scanner->getResults();
  deviceCount=results.getCount();
  for(int i=0;i<deviceCount;i++) {
    auto d=results.getDevice(i);auto o=arr.add<JsonObject>();
    o["address"]=d->getAddress().toString();o["type"]=d->getAddress().getType();
    o["name"]=d->getName();o["rssi"]=d->getRSSI();
    String hint="Unbekannt";
    if(d->isAdvertisingService(NimBLEUUID((uint16_t)0xffe0))) hint="AA55 / AA66";
    else if(d->isAdvertisingService(NimBLEUUID((uint16_t)0xfff0))) hint="ABBA / CBFF / Hcalory MVP1";
    else if(d->isAdvertisingService(NimBLEUUID((uint16_t)0xbd39))) hint="Hcalory MVP2";
    o["hint"]=hint;auto services=o["services"].to<JsonArray>();
    for(int j=0;j<d->getServiceUUIDCount();j++) services.add(d->getServiceUUID(j).toString());
  }
  devices="";serializeJson(doc,devices);scanning=false;hasScan=true;lastScan=millis();scanRevision++;
  logEvent("Bluetooth-Suche beendet: "+String(deviceCount)+" Geräte.");scanner->clearResults();
}
void finishWifiScan(int count) {
  JsonDocument doc;auto arr=doc.to<JsonArray>();
  for(int i=0;i<count&&i<30;i++) {
    String name=WiFi.SSID(i);if(name.isEmpty()) continue;
    bool open=WiFi.encryptionType(i)==WIFI_AUTH_OPEN, duplicate=false;
    for(auto row:arr) if(row["ssid"].as<String>()==name&&row["open"].as<bool>()==open) duplicate=true;
    if(duplicate) continue;
    auto row=arr.add<JsonObject>();row["ssid"]=name;row["rssi"]=WiFi.RSSI(i);row["open"]=open;
  }
  networks="";serializeJson(doc,networks);WiFi.scanDelete();wifiScanning=false;wifiHasScan=true;wifiRevision++;
  logEvent(count<0?"WLAN-Suche fehlgeschlagen.":"WLAN-Suche abgeschlossen.");
}
void status(JsonDocument& d) {
  bool ok=WiFi.status()==WL_CONNECTED;
  d["ventilationSupported"]=gatewaySupportsVentilation();d["version"]=VERSION;d["label"]=label;d["connected"]=ok;d["ssid"]=ssid;
  d["ip"]=ok?WiFi.localIP().toString():String("");d["rssi"]=ok?WiFi.RSSI():0;
  d["ap"]=ap;d["apIp"]=ap?WiFi.softAPIP().toString():String("");
  d["scanning"]=scanning;d["wifiScanning"]=wifiScanning;d["hasScan"]=hasScan;d["wifiHasScan"]=wifiHasScan;
  d["scanRevision"]=scanRevision;d["wifiRevision"]=wifiRevision;d["scanAge"]=hasScan?(millis()-lastScan)/1000:0;
  d["deviceCount"]=deviceCount;d["selected"]=selected;d["name"]=selectedName;d["hint"]=selectedHint;
  // Saving an address does not establish a connection. No protocol driver is active yet.
  d["heaterConnected"]=false;d["driverReady"]=false;
  d["freeHeap"]=ESP.getFreeHeap();d["minHeap"]=ESP.getMinFreeHeap();d["psram"]=ESP.getPsramSize();
  d["uptime"]=millis()/1000;d["logSequence"]=logSequence;
  mqttInfo(d["mqtt"].to<JsonObject>());
  firmwareUpdateInfo(d["update"].to<JsonObject>());
}
void setupRoutes() {
  const char* headers[]={"X-Gateway-Token","X-Update-Id","X-Update-Offset"};web.collectHeaders(headers,3);
  firmwareUpdateSetup(web,token);
  web.on("/",HTTP_GET,[]{
    web.sendHeader("Cache-Control","no-store");web.sendHeader("X-Content-Type-Options","nosniff");web.sendHeader("X-Frame-Options","DENY");
    web.send_P(200,"text/html; charset=utf-8",PAGE);
  });
  web.on("/api/status",HTTP_GET,[]{JsonDocument d;status(d);d["token"]=token;sendJson(d);});
  web.on("/api/devices",HTTP_GET,[]{web.send(200,"application/json",devices);});
  web.on("/api/networks",HTTP_GET,[]{web.send(200,"application/json",networks);});
  web.on("/api/events",HTTP_GET,[]{
    JsonDocument d;auto arr=d.to<JsonArray>();
    for(size_t i=0;i<eventCount;i++) {auto& e=events[(eventNext+24-eventCount+i)%24];auto row=arr.add<JsonObject>();row["at"]=e.at;row["message"]=e.message;}
    sendJson(d);
  });
  web.on("/api/diagnostics",HTTP_GET,[]{
    JsonDocument d;status(d);
    // Omit network names, device identifiers, passwords and the session token.
    d.remove("ssid");d.remove("selected");d.remove("name");d.remove("label");
    d["mqtt"].remove("deviceId");d["mqtt"].remove("baseTopic");
    web.sendHeader("Content-Disposition","attachment; filename=gateway-diagnose.json");sendJson(d);
  });
  web.on("/api/mqtt",HTTP_GET,[]{JsonDocument d;mqttInfo(d.to<JsonObject>(),true);sendJson(d);});
  web.on("/api/mqtt",HTTP_POST,[]{
    if(!allowed())return;
    JsonDocument d;
    const char* keys[]={"enabled","discovery","host","port","username","password","keepPassword","interval"};
    for(auto key:keys)d[key]=web.arg(key);
    String error;
    if(!mqttConfigure(d.as<JsonObjectConst>(),error)){web.send(400,"text/plain; charset=utf-8",error);return;}
    success(202);
  });
  web.on("/api/mqtt/discovery",HTTP_POST,[]{
    if(!allowed())return;
    if(!mqttRediscover()){web.send(409,"text/plain; charset=utf-8","MQTT muss verbunden und Home-Assistant-Erkennung aktiviert sein.");return;}
    success(202);
  });
  web.on("/api/mqtt/publish",HTTP_POST,[]{
    if(!allowed())return;
    if(!mqttPublishStatus()){web.send(409,"text/plain; charset=utf-8","MQTT nicht verbunden oder Sendepuffer voll.");return;}
    success(202);
  });
  web.on("/api/scan",HTTP_POST,[]{
    if(!allowed()||radioBusy()) return;
    if(!beginScan()) {web.send(503,"text/plain","Bluetooth nicht bereit.");return;}success(202);
  });
  web.on("/api/wifi-scan",HTTP_POST,[]{
    if(!allowed()||radioBusy()) return;
    int result=WiFi.scanNetworks(true);
    if(result==WIFI_SCAN_FAILED) {web.send(503,"text/plain","WLAN-Suche nicht bereit.");return;}
    wifiScanning=true;logEvent("WLAN-Suche gestartet.");success(202);
  });
  web.on("/api/select",HTTP_POST,[]{
    if(!allowed()||radioBusy()) return;
    JsonDocument d;deserializeJson(d,devices);
    for(auto v:d.as<JsonArray>()) if(web.arg("address")==v["address"].as<String>()) {
      selected=v["address"].as<String>();selectedName=v["name"].as<String>();selectedHint=v["hint"].as<String>();
      prefs.putString("heater",selected);prefs.putString("name",selectedName);prefs.putString("hint",selectedHint);prefs.putUChar("addrType",v["type"].as<uint8_t>());
      logEvent("Heizungsgerät gespeichert; Verbindung noch nicht geprüft.");success();return;
    }
    web.send(400,"text/plain; charset=utf-8","Gerät nicht in letzter Suche gefunden.");
  });
  web.on("/api/forget",HTTP_POST,[]{
    if(!allowed()||radioBusy()) return;
    selected="";selectedName="";selectedHint="";
    prefs.remove("heater");prefs.remove("name");prefs.remove("hint");prefs.remove("addrType");
    logEvent("Geräteauswahl aufgehoben.");success();
  });
  web.on("/api/settings",HTTP_POST,[]{
    if(!allowed()) return;
    String value=web.arg("label");value.trim();
    if(value.isEmpty()||value.length()>48) {web.send(400,"text/plain","Name muss 1 bis 48 Bytes lang sein.");return;}
    label=value;prefs.putString("label",label);mqttLabelChanged();logEvent("Anzeigename gespeichert.");success();
  });
  web.on("/api/wifi",HTTP_POST,[]{
    if(!allowed()||radioBusy()) return;
    String s=web.arg("ssid"),p=web.arg("password");
    bool keep=web.arg("keepPassword")=="true", open=web.arg("open")=="true";
    if(keep) {
      if(s!=ssid||ssid.isEmpty()) {web.send(400,"text/plain","Bei neuem WLAN bitte das Passwort eingeben.");return;}
      p=prefs.getString("pass","");
    } else if(open) p="";
    if(s.isEmpty()||s.length()>32||(!keep&&!open&&p.length()<8)||p.length()>63) {
      web.send(400,"text/plain; charset=utf-8","WLAN-Name: 1–32 Bytes. Passwort: 8–63 Zeichen. Offenes WLAN bitte ausdrücklich auswählen.");return;
    }
    prefs.putString("ssid",s);prefs.putString("pass",p);logEvent("WLAN-Einstellungen gespeichert; Neustart folgt.");success();rebootAt=millis()+1500;
  });
  web.on("/api/reboot",HTTP_POST,[]{if(!allowed()||radioBusy()) return;logEvent("Neustart angefordert.");success();rebootAt=millis()+1500;});
  web.onNotFound([]{
    if(web.uri().startsWith("/api/")){web.send(404,"text/plain","Nicht gefunden");return;}
    web.sendHeader("Location","/");web.send(302,"text/plain","");
  });
  web.begin();
}
void setup() {
  Serial.begin(115200);delay(1000);prefs.begin("gateway",false);
  ssid=prefs.getString("ssid","");selected=prefs.getString("heater","");selectedName=prefs.getString("name","");selectedHint=prefs.getString("hint","");
  label=prefs.getString("label","Meine Dieselheizung");
  char t[33];snprintf(t,sizeof(t),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());token=t;
  logEvent(String("Gateway gestartet · v")+VERSION);
  WiFi.mode(WIFI_STA);WiFi.setHostname("dieselheater");WiFi.setAutoReconnect(true);
  if(ssid.length()) WiFi.begin(ssid.c_str(),prefs.getString("pass","").c_str());else startAP();
  disconnectedAt=millis();NimBLEDevice::init("DieselHeater-Gateway");scanner=NimBLEDevice::getScan();
  scanner->setScanCallbacks(&scanCallbacks);scanner->setActiveScan(true);scanner->setInterval(160);scanner->setWindow(80);scanner->setMaxResults(80);
  mqttSetup(VERSION);setupRoutes();logEvent("Weboberfläche bereit.");
}
void loop() {
  web.handleClient();if(ap) dns.processNextRequest();if(scanDone.exchange(false)) finishScan();
  mqttLoop();firmwareUpdateLoop();
  if(wifiScanning) {int result=WiFi.scanComplete();if(result!=WIFI_SCAN_RUNNING) finishWifiScan(result);}
  uint32_t now=millis();bool ok=WiFi.status()==WL_CONNECTED;
  if(ok&&!connected) {connected=true;connectedAt=now;MDNS.begin("dieselheater");MDNS.addService("http","tcp",80);logEvent("WLAN verbunden: http://"+WiFi.localIP().toString());}
  if(!ok&&connected) {connected=false;disconnectedAt=now;MDNS.end();logEvent("WLAN-Verbindung unterbrochen.");}
  if(!ok&&!ap&&now-disconnectedAt>30000) startAP();
  if(ok&&ap&&now-connectedAt>60000) {dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);ap=false;logEvent("Setup-Hotspot ausgeschaltet.");}
  if(rebootAt&&(int32_t)(now-rebootAt)>=0) {mqttShutdown();ESP.restart();}
  while(Serial.available()) {
    char c=Serial.read();
    if(c=='\n') {
      serialLine.trim();if(serialLine=="STATUS") {JsonDocument d;status(d);serializeJson(d,Serial);Serial.println();}
      else if(serialLine=="SCAN") beginScan();else if(serialLine=="DEVICES") Serial.println(devices);serialLine="";
    } else if(c!='\r'&&serialLine.length()<64) serialLine+=c;
  }
  delay(2);
}
