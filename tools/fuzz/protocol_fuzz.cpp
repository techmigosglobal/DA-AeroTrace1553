#include "protocol.hpp"
#include <cstring>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,std::size_t size){
  if(size<24||size>256)return 0;aero::Record r;
  auto word=[&](unsigned p){return std::uint16_t(data[p])|(std::uint16_t(data[p+1])<<8);};
  r.commandWord=word(0);r.commandWord2=word(2);r.statusWord=word(4);r.statusWord2=word(6);r.validity=word(8);r.flags=word(10);r.vendorStatus=word(12);r.messageType=word(14);r.source=data[16]%3;r.channel=1+data[17]%2;r.bus=data[18]%2;
  for(unsigned i=24;i+1<size&&r.words.size()<32;i+=2)r.words.push_back(word(i));
  aero::Signal s;s.rt=data[19]%32;s.sa=data[20]%32;s.width=1+data[21]%64;s.lsb=data[22]%16;s.word=data[23]%32;s.transmit=bool(data[0]&1);s.signedValue=bool(data[1]&1);
  (void)aero::interpret(r);(void)aero::decode(r,s);(void)aero::protocolDiagnostics(r,aero::interpret(r));return 0;
}
