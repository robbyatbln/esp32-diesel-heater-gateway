#include "heater_protocol.h"
#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>
using namespace heater;
size_t unhex(const std::string& s,uint8_t* out){
  if(s=="-")return 0;
  if(s.size()%2||s.size()>128)return 0;
  for(size_t i=0;i<s.size()/2;i++){unsigned n;std::istringstream v(s.substr(i*2,2));if(!(v>>std::hex>>n))return 0;out[i]=n;}
  return s.size()/2;
}
int main(){
  std::string line;
  while(std::getline(std::cin,line)){
    std::istringstream in(line);char operation;int profile;std::string hex,mac;in>>operation>>profile;
    if(operation=='P'){
      in>>hex>>mac;uint8_t bytes[64]={};State s;s.target=777;
      auto r=parse(Profile(profile),bytes,0,s); // Empty input must not mutate output.
      if(r!=Result::InvalidPacket||s.target!=777)return 2;
      size_t n=unhex(hex,bytes);r=parse(Profile(profile),bytes,n,s,mac=="-"?nullptr:mac.c_str());
      std::cout<<int(r);
      if(r==Result::Ok)std::cout<<' '<<s.running<<' '<<s.step<<' '<<s.mode<<' '<<s.error<<' '<<s.unit<<' '<<s.hasLevel<<' '<<s.level<<' '<<s.hasTarget<<' '<<s.target<<' '<<std::setprecision(9)<<s.cabin<<' '<<s.body<<' '<<s.voltage<<' '<<s.altitude;
      std::cout<<'\n';
    }else if(operation=='C'){
      int cmd,arg,pin,f,h,m,sec,day;in>>cmd>>arg>>pin>>mac>>f>>h>>m>>sec>>day>>hex;
      Context c;c.pin=pin;c.fahrenheit=f;c.clockValid=day!=0;c.hour=h;c.minute=m;c.second=sec;c.weekday=day;
      if(mac!="-"&&mac.size()==12)for(int i=0;i<12;i++)c.mac[i]=mac[i];
      uint8_t bytes[64]={};State s;size_t n=unhex(hex,bytes);
      if(n&&parse(Profile(profile),bytes,n,s,mac=="-"?nullptr:mac.c_str())==Result::Ok)c.state=&s;
      Packet p;p.size=1;p.bytes[0]=123;auto r=encode(Profile(profile),Command(cmd),arg,c,p);
      if(r!=Result::Ok&&p.size!=0)return 3;
      std::cout<<int(r)<<' ';
      for(size_t i=0;i<p.size;i++)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<int(p.bytes[i]);
      std::cout<<std::dec<<'\n';
    }else return 4;
  }
}
