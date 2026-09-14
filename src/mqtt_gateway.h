#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void mqttSetup(const char* version);
void mqttLoop();
void mqttShutdown();
void mqttInfo(JsonObject out, bool includeConfig=false);
bool mqttConfigure(JsonObjectConst config, String& error);
bool mqttRediscover();
bool mqttPublishStatus();
void mqttLabelChanged();

// The future BLE driver must confirm commands before publishing real heater state.
bool gatewayHeaterCommand(const String& command, const String& value, String& error);
bool gatewayServiceCommand(const String& command, String& error);

bool gatewaySupportsVentilation();
