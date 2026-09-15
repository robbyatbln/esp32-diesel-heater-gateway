// Derived from diesel_heater_ble/protocol.py, upstream commit
// 0de895884f597975f2ef90638bf45e358db00519. See THIRD_PARTY_NOTICES.md.
#include "heater_protocol.h"
#include <string.h>
#include <math.h>

namespace heater {
namespace {
int clamp(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
int le(const uint8_t* d,int i){return d[i]|(d[i+1]<<8);}
int be(const uint8_t* d,int i){return (d[i]<<8)|d[i+1];}
int sign(int n){return n>=32768?n-65536:n;}
uint8_t checksum(const Packet& p,size_t start=0){uint8_t s=0;for(size_t i=start;i<p.size;i++)s+=p.bytes[i];return s;}
void finish(Packet& p,size_t start=0){auto s=checksum(p,start);p.bytes[p.size++]=s;}
bool validMac(const char* mac){
  if(!mac)return false;
  for(int i=0;i<12;i++)if(!((mac[i]>='0'&&mac[i]<='9')||(mac[i]>='a'&&mac[i]<='f')||(mac[i]>='A'&&mac[i]<='F')))return false;
  return mac[12]==0;
}
void xorCbff(uint8_t* d,size_t n,const char* mac){
  constexpr char key[]="passwordA2409PW";
  for(size_t i=0;i<n;i++){char c=mac[i%12];if(c>='a'&&c<='f')c-=32;d[i]^=key[i%15]^c;}
}
void feaa(Packet& p,int a,int b,const uint8_t* payload=nullptr,size_t size=0){
  const uint8_t header[]={0xfe,0xaa,0,0,uint8_t(size+9),0,uint8_t(a),uint8_t(b)};
  memcpy(p.bytes,header,8);p.size=8;if(size){memcpy(p.bytes+8,payload,size);p.size+=size;}finish(p);
}
void hcalory(Packet& p,int command,const uint8_t* payload,size_t size){
  const uint8_t header[]={0,2,0,1,0,1,0,uint8_t(command>>8),uint8_t(command),0,0,uint8_t(size)};
  memcpy(p.bytes,header,12);memcpy(p.bytes+12,payload,size);p.size=12+size;finish(p,8);
}
bool plausible(const State& s){return s.voltage>=0&&s.voltage<=100&&fabsf(s.cabin)<=500;}
void cbffFields(const uint8_t* d,State& s){
  s.running=(d[10]==2||d[10]==5||d[10]==6)?0:1;s.rawStatus=d[10];s.step=d[14];
  s.mode=d[11]<=3?d[11]:0;s.error=d[15]&63;s.unit=d[17]&15;
  if(s.mode==1||s.mode==3){s.level=clamp(d[12],1,10);s.hasLevel=true;}
  else{s.target=clamp(d[12],8,36);s.hasTarget=true;if(s.mode==2){s.level=clamp(d[13],1,10);s.hasLevel=true;}}
  s.cabin=sign(le(d,18));s.altitude=le(d,21)/10.f;s.voltage=le(d,23)/10.f;s.body=sign(le(d,25))/10.f;
}
}
const char* name(Profile p){
  static const char* names[]={"AA55","AA55 encrypted","AA66","AA66 encrypted","ABBA","CBFF / FEAA","Hcalory MVP1","Hcalory MVP2"};
  return unsigned(p)<8?names[unsigned(p)]:"Unknown";
}
bool ventilationCommandSupported(Profile p){return p==Profile::ABBA;}
Result parse(Profile p,const uint8_t* input,size_t n,State& out,const char* mac){
  // Invalid input never overwrites the caller's last known state.
  if(!input||n>64||unsigned(p)>7)return Result::InvalidPacket;
  uint8_t buffer[64];memcpy(buffer,input,n);const uint8_t* d=buffer;State s;
  if(p==Profile::AA55Encrypted||p==Profile::AA66Encrypted){
    if(n!=48)return Result::InvalidPacket;
    constexpr char key[]="password";for(size_t i=0;i<n;i++)buffer[i]^=key[i%8];
    if(d[0]!=0xaa||d[1]!=(p==Profile::AA55Encrypted?0x55:0x66))return Result::InvalidPacket;
    s.running=d[3];s.step=d[5];s.mode=d[8];s.rawStatus=d[3];
    s.error=d[p==Profile::AA55Encrypted?4:35];s.target=clamp(d[9],8,36);s.level=clamp(d[10],1,10);s.hasTarget=s.hasLevel=true;
    s.altitude=be(d,6)/10.f;s.voltage=be(d,11)/10.f;s.body=sign(be(d,13));s.cabin=sign(be(d,32))/10.f;
    if(p==Profile::AA66Encrypted){s.unit=d[27];if(s.unit==1)s.target=clamp(int(roundf((d[9]-32)*5.f/9)),8,36);}
  }else if(p==Profile::AA55||p==Profile::AA66){
    if((p==Profile::AA55?(n!=18&&n!=20):n!=20)||d[0]!=0xaa||d[1]!=(p==Profile::AA55?0x55:0x66))return Result::InvalidPacket;
    s.running=d[3];s.rawStatus=d[3];s.error=d[4];s.step=d[5];s.mode=d[8];
    s.altitude=p==Profile::AA55?le(d,6):d[6];s.voltage=le(d,11)/10.f;
    if(p==Profile::AA55){
      s.body=sign(le(d,13));s.cabin=sign(le(d,15));
      if(s.mode==1){s.level=d[9];s.hasLevel=true;}
      if(s.mode==2){s.target=d[9];s.hasTarget=true;}
      if(s.mode==0||s.mode==2){s.level=d[10]+1;s.hasLevel=true;}
    }else{
      s.body=le(d,13);if(s.body>350)s.body/=10;s.cabin=d[15];
      if(s.mode==1){s.level=clamp(d[9],1,10);s.hasLevel=true;}
      if(s.mode==2){s.target=clamp(d[9],8,36);s.hasTarget=true;}
    }
  }else if(p==Profile::ABBA){
    if(n<21||d[0]!=0xab||d[1]!=0xba)return Result::InvalidPacket;
    s.rawStatus=d[4];s.running=d[4]==1;s.step=d[4]==1?3:d[4]==2?4:d[4]==4?6:d[4]==6?0:d[4];
    if(d[5]==255)s.error=d[6];else{
      s.mode=d[5]==0?1:d[5]==1?2:d[5];
      if(s.mode==1){s.level=clamp(d[6],1,10);s.hasLevel=true;}else{s.target=clamp(d[6],8,36);s.hasTarget=true;}
    }
    s.unit=d[10];s.cabin=int(d[11])-(s.unit==1?22:30);s.body=be(d,12);s.voltage=d[9];s.altitude=le(d,16);
  }else if(p==Profile::CBFF){
    if(n!=46&&n!=47)return Result::InvalidPacket;
    if(d[0]!=0xcb||d[1]!=0xff){if(!validMac(mac))return Result::InvalidPacket;xorCbff(buffer,n,mac);}
    if(d[0]!=0xcb||d[1]!=0xff)return Result::InvalidPacket;
    cbffFields(d,s);if(!plausible(s))return Result::InvalidPacket;
  }else{
    // Authentication responses are not telemetry. Header and minimum size only:
    // upstream has no verified telemetry checksum/length framing for MVP1/MVP2.
    if(n<38||d[0]!=0||(d[1]!=2&&d[1]!=3)||(d[7]==0x0b&&d[8]==0x0c))return Result::InvalidPacket;
    s.rawStatus=d[20]>>4;int step=s.rawStatus==4?4:d[20]&15;
    s.running=s.rawStatus!=0&&s.rawStatus!=15;s.step=step==1?6:step==3?2:step==5?3:step==7?0:step;
    s.mode=d[21]==1?2:d[21]==2?1:0;
    if(d[20]!=0&&s.rawStatus!=4&&s.rawStatus!=15){
      if(s.mode==2){s.target=d[22];s.hasTarget=true;}else{s.level=clamp(d[22],1,10);s.hasLevel=true;}
    }
    s.error=s.rawStatus==15?d[22]:0;s.unit=d[37];s.voltage=be(d,24)/10.f;s.body=be(d,27)/10;s.cabin=be(d,30)/10;
  }
  out=s;return Result::Ok;
}
Result encode(Profile p,Command cmd,int arg,const Context& c,Packet& out){
  out=Packet{};Packet b;
  if(unsigned(p)>7||c.pin>9999)return Result::InvalidArgument;
  if((cmd==Command::Query||cmd==Command::Authenticate)&&arg!=0)return Result::InvalidArgument;
  bool h=p==Profile::MVP1||p==Profile::MVP2;
  if(cmd==Command::Power&&(arg<0||arg>1))return Result::InvalidArgument;
  if(cmd==Command::Mode&&(arg<1||arg>3))return Result::InvalidArgument;
  if(cmd==Command::Level&&(arg<1||arg>10))return Result::InvalidArgument;
  if(cmd==Command::Temperature&&(arg<(h?(c.fahrenheit?32:0):8)||arg>(h?(c.fahrenheit?104:40):36)))return Result::InvalidArgument;
  if(cmd==Command::Mode&&arg==3&&!ventilationCommandSupported(p))return Result::Unsupported;
  // AAxx and ABBA reuse command 4 for the parameter of the current mode.
  // This mapping lives in upstream coordinator.async_set_level, not its codec.
  if(unsigned(p)<=4&&(cmd==Command::Level||cmd==Command::Temperature)){
    if(!c.state||c.state->error||c.state->mode!=(cmd==Command::Level?1:2))return Result::NeedState;
  }
  if(unsigned(p)<=3){
    if(cmd!=Command::Query&&cmd!=Command::Mode&&cmd!=Command::Power&&cmd!=Command::Temperature&&cmd!=Command::Level)return Result::Unsupported;
    uint8_t a[]={0xaa,0x55,uint8_t(c.pin/100),uint8_t(c.pin%100),uint8_t(cmd==Command::Level?4:uint8_t(cmd)),uint8_t(arg),uint8_t(arg>>8)};
    memcpy(b.bytes,a,7);b.size=7;finish(b,2);
  }else if(p==Profile::ABBA){
    uint8_t a[]={0xba,0xab,4,0xbb,0,0,0};
    switch(cmd){
      case Command::Query:a[3]=0xcc;break;
      case Command::Power:
        // Heat is a toggle; never generate it without an unambiguous state.
        if(!c.state||c.state->error)return Result::NeedState;
        if(c.state->rawStatus==4||c.state->rawStatus==2)return Result::Unsupported;
        if(c.state->rawStatus!=0&&c.state->rawStatus!=1&&c.state->rawStatus!=6)return Result::NeedState;
        if((c.state->rawStatus==1)==bool(arg))return Result::NoChange;
        a[4]=0xa1;break;
      case Command::Temperature:
      case Command::Level:a[3]=0xdb;a[4]=arg;break;
      case Command::Mode:
        if(arg==3&&(!c.state||c.state->error||(c.state->rawStatus!=0&&c.state->rawStatus!=6)))return Result::NeedState;
        a[4]=arg==2?0xac:arg==3?0xa4:0xad;break;
      default:return Result::Unsupported;
    }
    memcpy(b.bytes,a,7);b.size=7;finish(b);
  }else if(p==Profile::CBFF){
    if(!validMac(c.mac))return Result::InvalidArgument;
    uint8_t payload[]={0,0,255,255};
    if(cmd==Command::Authenticate){payload[0]=c.pin%100;payload[1]=c.pin/100;feaa(b,6,0,payload,2);}
    else if(cmd==Command::Query)feaa(b,0,0);
    else{
      if(cmd==Command::Power){
        if(!c.state)return Result::NeedState;
        auto s=*c.state;
        if(s.mode==2&&s.hasTarget){payload[0]=2;payload[1]=s.target;}
        else if((s.mode==1||s.mode==3)&&s.hasLevel){payload[0]=s.mode;payload[1]=s.level;}
        else return Result::NeedState;
        if((payload[0]==2&&(payload[1]<8||payload[1]>36))||(payload[0]!=2&&(payload[1]<1||payload[1]>10)))return Result::NeedState;
      }else if(cmd==Command::Temperature){payload[0]=2;payload[1]=arg;}
      else if(cmd==Command::Level){payload[0]=1;payload[1]=arg;}
      else if(cmd==Command::Mode){
        // Require a known target instead of inventing level 5 / 21 degrees.
        if(!c.state)return Result::NeedState;
        payload[0]=arg;
        if(arg==1&&c.state->hasLevel)payload[1]=c.state->level;
        else if(arg==2&&c.state->hasTarget)payload[1]=c.state->target;
        else return Result::NeedState;
        if((arg==1&&(payload[1]<1||payload[1]>10))||(arg==2&&(payload[1]<8||payload[1]>36)))return Result::NeedState;
      }else return Result::Unsupported;
      feaa(b,1,cmd==Command::Power?arg:1,payload,4);
    }
    xorCbff(b.bytes,b.size,c.mac);
  }else{
    uint8_t payload[9]={};
    if(cmd==Command::Authenticate){
      if(p!=Profile::MVP2)return Result::Unsupported;
      uint8_t pin[]={1,uint8_t(c.pin/1000),uint8_t(c.pin/100%10),uint8_t(c.pin/10%10),uint8_t(c.pin%10)};
      hcalory(b,0x0a0c,pin,5);
    }else if(cmd==Command::Query&&p==Profile::MVP2){
      if(!c.clockValid||c.hour>23||c.minute>59||c.second>59||c.weekday<1||c.weekday>7)return Result::NeedClock;
      uint8_t t[]={c.hour,c.minute,c.second,c.weekday,0};hcalory(b,0x0a0a,t,5);
      b.size--;finish(b); // MVP2 query uniquely sums the entire packet.
    }else if(cmd==Command::Temperature){payload[0]=arg;payload[1]=c.fahrenheit;hcalory(b,0x0706,payload,2);}
    else if(cmd==Command::Level){payload[0]=arg;hcalory(b,0x0607,payload,1);}
    else{
      if(cmd==Command::Power)payload[8]=arg?2:1;
      else if(cmd==Command::Mode)payload[8]=arg==2?6:7;
      else if(cmd!=Command::Query)return Result::Unsupported;
      hcalory(b,0x0e04,payload,9);
    }
  }
  out=b;return Result::Ok;
}
} // namespace heater
