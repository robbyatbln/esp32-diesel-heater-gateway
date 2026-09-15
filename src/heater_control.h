#pragma once
#include "heater_protocol.h"
namespace heater {
bool fanState(const State&,int profile);
bool offState(const State&,int profile);
bool cooldownState(const State&,int profile);
const char* controlError(const State&,int profile,bool fresh,bool pending,Command,int argument);
bool confirms(const State&,int profile,Command,int argument,uint32_t receivedAt,uint32_t sentAt);
}
