#include "heater_control.h"
namespace heater {
bool fanState(const State& s,int p){return p==4?s.rawStatus==4:p>=6?s.rawStatus==12:s.mode==3&&s.running;}
bool offState(const State& s,int p){return p==4?(s.rawStatus==0||s.rawStatus==6):p>=6?(s.rawStatus==0||s.rawStatus==15):!s.running;}
bool cooldownState(const State& s,int p){return p==4?s.rawStatus==2:p>=6?s.rawStatus==4:s.step==4;}
const char* controlError(const State& s,int p,bool fresh,bool pending,Command cmd,int arg){
  if(!fresh)return "heater_state_unavailable";
  if(pending)return "command_pending";
  bool stop=cmd==Command::Power&&arg==0;
  if(s.error&&!stop)return "heater_reports_error";
  if(cooldownState(s,p)&&!stop)return "cooldown_in_progress";
  if(fanState(s,p)&&!stop)return "stop_ventilation_first";
  if(p==5&&cmd!=Command::Power&&offState(s,p))return "start_heater_before_setting";
  if(p<6&&s.unit==1&&cmd==Command::Temperature)return "set_heater_to_celsius_first";
  return "";
}
bool confirms(const State& s,int p,Command cmd,int arg,uint32_t receivedAt,uint32_t sentAt){
  // Signed subtraction handles both queued pre-command responses and millis wrap.
  if(int32_t(receivedAt-sentAt)<250)return false;
  if(cmd==Command::Power)return arg?!offState(s,p)&&!cooldownState(s,p)&&!fanState(s,p)&&!s.error:offState(s,p)||cooldownState(s,p);
  if(s.error)return false;
  if(cmd==Command::Mode)return arg==3?fanState(s,p):s.mode==arg&&!fanState(s,p);
  if(cmd==Command::Level)return s.hasLevel&&s.level==arg;
  if(cmd==Command::Temperature)return s.hasTarget&&s.target==arg;
  return false;
}
}
