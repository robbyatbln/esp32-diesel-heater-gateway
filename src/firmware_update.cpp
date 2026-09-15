#include "firmware_update.h"
#include "heater_gateway.h"
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <mbedtls/sha256.h>

extern bool scanning, wifiScanning;
extern uint32_t rebootAt;
extern void logEvent(const String& message);

namespace {
WebServer* server;
String csrf, repository, session, expectedHash, lastError;
Preferences settings;
esp_ota_handle_t handle=0;
const esp_partition_t* partition=nullptr;
mbedtls_sha256_context hash;
bool active=false, ready=false, hashOpen=false, chunkAccepted=false, seenFile=false, markerFound=false;
size_t expected=0, received=0, chunkBytes=0, markerPosition=0;
uint32_t touched=0;
int chunkStatus=400;

bool decimal(const String& value,size_t& result) {
  if(value.isEmpty()||value.length()>9)return false;
  result=0;for(char c:value){if(c<'0'||c>'9')return false;result=result*10+c-'0';}return true;
}
bool validRepo(const String& value) {
  if(value.isEmpty())return true;
  int slash=value.indexOf('/');
  if(slash<1||slash>39||slash!=value.lastIndexOf('/')||value.length()>140||slash==value.length()-1)return false;
  for(char c:value)if(!isalnum((unsigned char)c)&&c!='-'&&c!='_'&&c!='.'&&c!='/')return false;
  return value.indexOf("..")<0;
}
void reply(int code,const String& message) {server->send(code,"text/plain; charset=utf-8",message);}
bool authorized() {
  if(server->header("X-Gateway-Token")==csrf)return true;
  reply(403,"Sitzung abgelaufen. Bitte Seite neu laden.");return false;
}
void abortUpdate(const String& reason) {
  if(handle){esp_ota_abort(handle);handle=0;}
  if(hashOpen){mbedtls_sha256_free(&hash);hashOpen=false;}
  active=false;session="";lastError=reason;seenFile=false;chunkAccepted=false;
}
void infoResponse() {
  JsonDocument d;firmwareUpdateInfo(d.to<JsonObject>());String body;serializeJson(d,body);
  server->sendHeader("Cache-Control","no-store");server->send(200,"application/json",body);
}
bool validSession() {return active&&!session.isEmpty()&&server->header("X-Update-Id")==session;}
void chunkUpload() {
  HTTPUpload& file=server->upload();
  if(file.status==UPLOAD_FILE_START) {
    if(seenFile){chunkAccepted=false;chunkStatus=400;abortUpdate("Nur eine Datei pro Datenblock erlaubt.");return;}
    seenFile=true;chunkBytes=0;chunkAccepted=false;chunkStatus=403;
    if(server->header("X-Gateway-Token")!=csrf||!validSession())return;
    size_t offset=0;
    if(!decimal(server->header("X-Update-Offset"),offset)||offset!=received){chunkStatus=409;return;}
    chunkAccepted=true;chunkStatus=200;touched=millis();
  } else if(file.status==UPLOAD_FILE_WRITE&&chunkAccepted) {
    size_t count=file.currentSize;
    if(chunkBytes+count>16384||received+count>expected){chunkStatus=400;chunkAccepted=false;abortUpdate("Firmwaregröße stimmt nicht.");return;}
    if(received==0) {
      // App descriptor distinguishes an app from a merged bootloader image.
      if(count<36||file.buf[0]!=ESP_IMAGE_HEADER_MAGIC||file.buf[12]!=9||file.buf[13]!=0||
         file.buf[32]!=0x32||file.buf[33]!=0x54||file.buf[34]!=0xcd||file.buf[35]!=0xab) {
        chunkStatus=400;chunkAccepted=false;abortUpdate("Keine ESP32-S3-App-Datei. Bitte die Gateway-Update-Datei auswählen.");return;
      }
    }
    if(esp_ota_write(handle,file.buf,count)!=ESP_OK){chunkStatus=500;chunkAccepted=false;abortUpdate("Schreiben der Firmware fehlgeschlagen.");return;}
    mbedtls_sha256_update_ret(&hash,file.buf,count);
    for(size_t i=0;i<count&&!markerFound;i++) {
      char c=(char)file.buf[i];
      if(c==FIRMWARE_ID[markerPosition]){if(++markerPosition==strlen(FIRMWARE_ID))markerFound=true;}
      else markerPosition=c==FIRMWARE_ID[0]?1:0;
    }
    received+=count;chunkBytes+=count;touched=millis();
  } else if(file.status==UPLOAD_FILE_ABORTED&&chunkAccepted) {
    chunkAccepted=false;chunkStatus=400;abortUpdate("Übertragung unterbrochen. Bitte erneut starten.");
  }
}
}

bool firmwareUpdateBusy(){return active||ready;}
void firmwareUpdateInfo(JsonObject out) {
  out["repository"]=repository;out["active"]=active;out["ready"]=ready;
  out["received"]=received;out["size"]=expected;out["error"]=lastError;
  const esp_partition_t* next=esp_ota_get_next_update_partition(nullptr);
  out["maxSize"]=next?next->size:0;out["target"]="xiao_esp32s3";
  out["firmwareId"]=FIRMWARE_ID;
}
void firmwareUpdateSetup(WebServer& web,const String& token) {
  server=&web;csrf=token;settings.begin("updates",false);repository=settings.getString("repo","");
  web.on("/api/update",HTTP_GET,infoResponse);
  web.on("/api/update/repository",HTTP_POST,[]{
    if(!authorized())return;
    if(firmwareUpdateBusy()){reply(409,"Update läuft bereits.");return;}
    String value=server->arg("repository");value.trim();
    if(value.startsWith("https://github.com/"))value.remove(0,19);
    if(value.endsWith("/"))value.remove(value.length()-1);
    if(value.endsWith(".git"))value.remove(value.length()-4);
    if(!validRepo(value)){reply(400,"Bitte GitHub-Repository als Benutzer/Projekt angeben.");return;}
    if(settings.putString("repo",value)!=value.length()){reply(500,"Repository konnte nicht gespeichert werden.");return;}
    repository=value;infoResponse();
  });
  web.on("/api/update/start",HTTP_POST,[]{
    if(!authorized())return;
    if(firmwareUpdateBusy()||scanning||wifiScanning||rebootAt||heaterRadioBusy()){reply(409,"Bitte zuerst die Heizungsverbindung trennen und laufende Suchen abwarten.");return;}
    size_t size=0;String checksum=server->arg("sha256");checksum.toLowerCase();
    partition=esp_ota_get_next_update_partition(nullptr);
    bool hex=checksum.length()==64;for(char c:checksum)if(!isxdigit((unsigned char)c))hex=false;
    if(!decimal(server->arg("size"),size)||size<1024||!partition||size>partition->size||!hex){reply(400,"Firmwaregröße oder Prüfsumme ungültig.");return;}
    // Sequential erasure avoids blocking the web handler for the whole slot.
    if(esp_ota_begin(partition,OTA_WITH_SEQUENTIAL_WRITES,&handle)!=ESP_OK){handle=0;reply(500,"Update-Speicher konnte nicht geöffnet werden.");return;}
    expected=size;expectedHash=checksum;received=0;markerFound=false;markerPosition=0;lastError="";seenFile=false;chunkAccepted=false;
    mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);hashOpen=true;
    char id[33];snprintf(id,sizeof(id),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());session=id;
    active=true;touched=millis();logEvent("Firmware-Übertragung gestartet.");
    server->send(200,"application/json",String("{\"session\":\"")+session+"\",\"chunkSize\":16384}");
  });
  // The completion handler resets request-local upload state, including rejected requests.
  web.on("/api/update/chunk",HTTP_POST,[]{
    bool success=chunkAccepted&&chunkBytes>0&&active;int code=chunkStatus;
    seenFile=false;chunkAccepted=false;
    if(!success){reply(code==200?400:code,"Datenblock abgewiesen. Bitte Update erneut starten.");return;}
    server->send(200,"application/json",String("{\"received\":")+received+"}");
  },chunkUpload);
  web.on("/api/update/cancel",HTTP_POST,[]{
    if(!authorized())return;
    if(!validSession()){reply(409,"Keine passende Update-Sitzung.");return;}
    abortUpdate("Update abgebrochen.");infoResponse();
  });
  web.on("/api/update/finish",HTTP_POST,[]{
    if(!authorized())return;
    if(!validSession()){reply(409,"Keine passende Update-Sitzung.");return;}
    if(received!=expected||!markerFound){abortUpdate("Datei unvollständig oder keine passende Gateway-Firmware.");reply(400,lastError);return;}
    unsigned char digest[32];char checksum[65];mbedtls_sha256_finish_ret(&hash,digest);mbedtls_sha256_free(&hash);hashOpen=false;
    for(int i=0;i<32;i++)snprintf(checksum+i*2,3,"%02x",digest[i]);
    if(expectedHash!=checksum){abortUpdate("Prüfsumme stimmt nicht. Bisherige Firmware bleibt aktiv.");reply(400,lastError);return;}
    esp_err_t error=esp_ota_end(handle);handle=0;
    if(error!=ESP_OK){abortUpdate("Firmwareprüfung fehlgeschlagen. Bisherige Firmware bleibt aktiv.");reply(400,lastError);return;}
    if(esp_ota_set_boot_partition(partition)!=ESP_OK){abortUpdate("Neue Firmware konnte nicht aktiviert werden.");reply(500,lastError);return;}
    active=false;ready=true;session="";logEvent("Firmware geprüft. Neustart folgt.");
    server->send(200,"application/json","{\"ok\":true,\"rebooting\":true}");rebootAt=millis()+1800;
  });
}
void firmwareUpdateLoop() {
  if(active&&millis()-touched>45000){abortUpdate("Update wegen Zeitüberschreitung abgebrochen.");seenFile=false;chunkAccepted=false;}
}
