// Protocol port: see THIRD_PARTY_NOTICES.md (MIT, Sebastian Majchrzak).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace heater {
enum class Profile : uint8_t { AA55, AA55Encrypted, AA66, AA66Encrypted, ABBA, CBFF, MVP1, MVP2 };
enum class Result : uint8_t { Ok, InvalidPacket, InvalidArgument, Unsupported, NeedState, NeedClock, NoChange };
enum class Command : uint8_t { Query=1, Mode=2, Power=3, Temperature=4, Level=5, Authenticate=100 };
// Values mirror the upstream codec, including native temperature units. The
// transport/UI adapter must convert units and apply model calibration separately.
struct State {
  int running=0, step=0, mode=0, error=0, unit=0, rawStatus=0;
  int level=0, target=0;
  bool hasLevel=false, hasTarget=false;
  float cabin=0, body=0, voltage=0, altitude=0;
};
struct Context {
  uint16_t pin=1234;
  char mac[13]={}; // CBFF: twelve hexadecimal digits, without separators.
  bool fahrenheit=false;
  const State* state=nullptr; // Must be fresh and from this device/connection.
  bool clockValid=false;
  uint8_t hour=0, minute=0, second=0, weekday=0; // Local time, ISO weekday 1..7.
};
struct Packet { uint8_t bytes[64]={}; size_t size=0; };
const char* name(Profile profile);
Result parse(Profile profile,const uint8_t* data,size_t length,State& out,const char* mac=nullptr);
Result encode(Profile profile,Command command,int argument,const Context& context,Packet& out);
bool ventilationCommandSupported(Profile profile);
} // namespace heater
