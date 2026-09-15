#include "heater_control.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace heater;
int main(){
  State s;s.running=1;s.mode=2;s.hasTarget=true;s.target=21;s.hasLevel=true;s.level=4;
  assert(!confirms(s,0,Command::Temperature,21,999,1000));
  assert(!confirms(s,0,Command::Temperature,21,1100,1000));
  assert(confirms(s,0,Command::Temperature,21,1300,1000));
  assert(confirms(s,0,Command::Temperature,21,300,0xffffff00));
  assert(!confirms(s,0,Command::Temperature,22,1300,1000));
  assert(!strcmp(controlError(s,0,false,false,Command::Power,1),"heater_state_unavailable"));
  assert(!strcmp(controlError(s,0,true,true,Command::Power,1),"command_pending"));
  s.error=1;assert(!confirms(s,0,Command::Temperature,21,1300,1000));
  assert(*controlError(s,0,true,false,Command::Power,1));
  assert(!*controlError(s,0,true,false,Command::Power,0));s.error=0;
  s.step=4;assert(*controlError(s,0,true,false,Command::Power,1));
  assert(confirms(s,0,Command::Power,0,1300,1000));s.step=3;
  s.running=0;assert(*controlError(s,5,true,false,Command::Temperature,21));
  assert(!*controlError(s,5,true,false,Command::Power,1));
  s.running=1;s.mode=3;assert(*controlError(s,5,true,false,Command::Mode,2));
  assert(!confirms(s,5,Command::Power,1,1300,1000));
  s.rawStatus=4;assert(fanState(s,4));assert(*controlError(s,4,true,false,Command::Mode,1));
  s.rawStatus=12;assert(fanState(s,7));
  s.rawStatus=4;assert(cooldownState(s,7));s.rawStatus=0;assert(offState(s,7));
  s.unit=1;s.mode=2;s.rawStatus=1;
  assert(*controlError(s,4,true,false,Command::Temperature,21));
  assert(!*controlError(s,7,true,false,Command::Temperature,21));
  std::cout<<"PASS: control guards, state confirmation, stale responses and clock wrap\n";
}
