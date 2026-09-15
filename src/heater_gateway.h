#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void heaterSetup();
void heaterLoop();
void heaterInfo(JsonObject out);
void heaterMqttState(JsonObject out);
bool heaterConfigure(const String& profile,const String& pin,bool keepPin,String& error);
bool heaterConnect(const String& address,uint8_t addressType,const String& localEpoch,String& error);
void heaterDisconnect();
void heaterResetConfiguration();
bool heaterRadioBusy();
bool heaterAvailable();
bool heaterControlsReady();
uint32_t heaterRevision();
bool heaterCommand(const String& command,const String& value,String& error);
bool heaterVentilationSupported();
