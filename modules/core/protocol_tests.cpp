#include "protocol.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
using namespace aero;
namespace {
Record hw(unsigned rt=3,bool tx=false,unsigned count=2) {
  Record r;r.source=1;r.messageType=tx?1:0;r.validity=5;r.commandWord=(rt<<11)|(tx<<10)|(2<<5)|count;r.statusWord=rt<<11;r.words={1200,2000};return r;
}
}
int main(){try{
  auto r=hw();CHECK(interpret(r).payloadEligible && interpret(r).receiverRt==3);
  Signal signal{"X","u"};CHECK(*decode(r,signal)==12);
  r=hw(0,true);r.statusWord=0;CHECK(interpret(r).valid && interpret(r).statuses[0].present);
  r.validity=1;CHECK(!interpret(r).payloadEligible);
  r=hw();r.messageType=15;CHECK(!interpret(r).supported && !decode(r,signal));
  r=hw();r.messageType=1;CHECK(!interpret(r).valid);
  r=hw();r.statusWord|=0x100;CHECK(interpret(r).payloadEligible);CHECK(statusDescription(interpret(r)).find("Service request")!=std::string::npos);
  for(auto bits:{0x400,0x008,0x004,0x001,0x020}){r=hw();r.statusWord|=bits;CHECK(!interpret(r).payloadEligible && !decode(r,signal));}
  for(auto bits:{0x200,0x100,0x010,0x002}){r=hw();r.statusWord|=bits;CHECK(interpret(r).payloadEligible);}
  r=hw();r.flags=8;r.vendorStatus=8;CHECK(protocolDiagnostics(r,interpret(r)).find("parity/Manchester detail unavailable")!=std::string::npos);
  r=hw();r.words.pop_back();CHECK(!interpret(r).valid);
  r=hw();r.words.resize(32);r.commandWord&=~31;CHECK(interpret(r).expectedWords==32 && interpret(r).valid);
  r=hw();r.commandWord=(31<<11)|(2<<5)|2;r.messageType=8;r.validity=1;CHECK(interpret(r).valid && interpret(r).direction=="BC→Broadcast");
  r.validity=5;r.statusWord=31<<11;CHECK(!interpret(r).valid);
  r=hw();r.messageType=2;r.validity=15;r.commandWord2=(4<<11)|(1<<10)|(7<<5)|2;r.statusWord=4<<11;r.statusWord2=3<<11;
  auto p=interpret(r);CHECK(p.valid && p.rtToRt && p.payloadCommand.rt==4 && p.receiverRt==3 && p.transmitterRt==4);
  signal.rt=4;signal.sa=7;signal.transmit=true;CHECK(*decode(r,signal)==12);
  signal.rt=3;signal.sa=2;signal.transmit=false;CHECK(!decode(r,signal));
  std::swap(r.statusWord,r.statusWord2);CHECK(!interpret(r).valid);std::swap(r.statusWord,r.statusWord2);
  r.commandWord2^=1;CHECK(!interpret(r).valid);r.commandWord2^=1;
  r.messageType=10;r.commandWord=(31<<11)|(2<<5)|2;r.validity=7;CHECK(interpret(r).valid && interpret(r).direction=="RT→Broadcast");
  r.validity=15;CHECK(!interpret(r).valid);
  struct Mode {unsigned code;bool tx,data,broadcast;};
  const Mode modes[]={{0,1,0,0},{1,1,0,1},{2,1,0,0},{3,1,0,1},{4,1,0,1},{5,1,0,1},{6,1,0,1},{7,1,0,1},{8,1,0,1},{16,1,1,0},{17,0,1,1},{18,1,1,0},{19,1,1,0},{20,0,1,1},{21,0,1,1}};
  for(auto m:modes)for(unsigned sa:{0,31}) {
    r=hw();r.commandWord=(3<<11)|(m.tx<<10)|(sa<<5)|m.code;r.messageType=m.data?(m.tx?7:6):5;r.words.resize(m.data?1:0);
    CHECK(interpret(r).valid && interpret(r).modeCode==m.code && interpret(r).expectedWords==unsigned(m.data));
    r.commandWord^=1<<10;CHECK(!interpret(r).valid);r.commandWord^=1<<10;
    r.commandWord=(31<<11)|(m.tx<<10)|(sa<<5)|m.code;r.messageType=m.data?14:13;r.validity=1;
    CHECK(interpret(r).valid==m.broadcast);
  }
  r=hw();r.commandWord=(3<<11)|(1<<10)|9;r.messageType=5;r.words.clear();CHECK(!interpret(r).valid);
  r=hw();r.validity=4;CHECK(!interpret(r).valid);
  CHECK(protocolQuality(r,interpret(r),true)=="CLOCK BACKWARD");
  std::cout<<"PASS: all defined message/mode types, status meanings, RT0, RT-to-RT routing, broadcasts, invalid payload rejection\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
