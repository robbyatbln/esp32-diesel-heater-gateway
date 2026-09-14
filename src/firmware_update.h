#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

void firmwareUpdateSetup(WebServer& server, const String& token);
void firmwareUpdateLoop();
bool firmwareUpdateBusy();
void firmwareUpdateInfo(JsonObject out);
// Shared by the file checker and release packaging; this is not a signature.
constexpr char FIRMWARE_ID[] = "DIESELHEATER_GATEWAY:XIAO_ESP32S3:OTA1";
