#include "protocol.hpp"

namespace aero {
namespace {
struct Mode { unsigned code; std::string_view name; bool transmit, data, broadcast; };
constexpr Mode modes[]{
  {0,"Dynamic bus control",true,false,false},{1,"Synchronize",true,false,true},
  {2,"Transmit status word",true,false,false},{3,"Initiate self-test",true,false,true},
  {4,"Transmitter shutdown",true,false,true},{5,"Override transmitter shutdown",true,false,true},
  {6,"Inhibit terminal flag",true,false,true},{7,"Override inhibit terminal flag",true,false,true},
  {8,"Reset remote terminal",true,false,true},{16,"Transmit vector word",true,true,false},
  {17,"Synchronize with data word",false,true,true},{18,"Transmit last command word",true,true,false},
  {19,"Transmit BIT word",true,true,false},{20,"Selected transmitter shutdown",false,true,true},
  {21,"Override selected transmitter shutdown",false,true,true}
};
bool knownType(unsigned type) {
  switch(type){case 0:case 1:case 2:case 5:case 6:case 7:case 8:case 10:case 13:case 14:return true;default:return false;}
}
}
ProtocolInfo interpret(const Record &r) {
  ProtocolInfo p;p.first=command(r.commandWord);p.second=command(r.commandWord2);
  p.commandPresent=!r.source || (r.validity&1);p.command2Present=(r.validity&2)!=0;
  p.actualWords=unsigned(r.words.size());p.payloadCommand=p.first;p.broadcast=p.first.broadcast;p.mode=p.first.mode;
  p.rtToRt=p.command2Present || (r.source && (r.messageType==2 || r.messageType==10));
  p.statuses={StatusInfo{bool((!r.source && (r.statusWord || p.first.rt==0)) || (r.validity&4)),unsigned(r.statusWord>>11),std::uint16_t(r.statusWord&0x7ff)},
              StatusInfo{bool(r.validity&8),unsigned(r.statusWord2>>11),std::uint16_t(r.statusWord2&0x7ff)}};
  // Simulator/ATD1 records do not carry SDK message-type evidence. Hardware does.
  if(r.source && !knownType(r.messageType))return p;
  p.supported=true;
  auto reject=[&](std::string_view reason){p.reason=reason;return p;};
  if(!p.commandPresent)return reject("Command word absent");
  if(p.rtToRt) {
    p.transaction=p.broadcast?"Broadcast RT-to-RT":"RT-to-RT";p.direction=p.broadcast?"RT→Broadcast":"RT→RT";
    p.payloadCommand=p.second;p.transmitterRt=p.second.rt;p.receiverRt=p.first.rt;p.expectedWords=p.second.count;
    if(!p.command2Present || p.first.transmit || !p.second.transmit || p.first.mode || p.second.mode || p.second.broadcast ||
       p.first.rt==p.second.rt || p.first.count!=p.second.count)return reject("Inconsistent RT-to-RT commands");
  } else if(p.mode) {
    p.transaction=p.broadcast?"Broadcast mode command":"Mode command";p.direction=p.first.transmit?"RT→BC":"BC→RT";
    if(p.broadcast)p.direction="BC→Broadcast";
    p.modeCode=p.first.count;const Mode *selected=nullptr;for(const auto &m:modes)if(m.code==p.modeCode){selected=&m;break;}
    if(!selected)return reject("Reserved mode code");
    p.modeName=selected->name;p.expectedWords=selected->data?1:0;
    p.transmitterRt=p.first.transmit?p.first.rt:-1;p.receiverRt=p.first.transmit?-1:int(p.first.rt);
    if(p.broadcast){p.transmitterRt=-1;p.receiverRt=31;}
    if(p.first.transmit!=selected->transmit || (p.broadcast&&!selected->broadcast))return reject("Illegal mode direction or broadcast");
  } else {
    p.transaction=p.broadcast?"Broadcast BC-to-RT":p.first.transmit?"RT-to-BC":"BC-to-RT";
    p.direction=p.broadcast?"BC→Broadcast":p.first.transmit?"RT→BC":"BC→RT";p.expectedWords=p.first.count;
    p.transmitterRt=p.first.transmit?p.first.rt:-1;p.receiverRt=p.first.transmit?-1:int(p.first.rt);
    if(p.broadcast&&p.first.transmit)return reject("Broadcast RT cannot transmit");
  }
  if(r.source) {
    const unsigned derived=p.rtToRt?(p.broadcast?10:2):p.mode?(p.broadcast?(p.expectedWords?14:13):(p.expectedWords?(p.first.transmit?7:6):5)):(p.broadcast?8:(p.first.transmit?1:0));
    if(r.messageType!=derived)return reject("SDK message type disagrees with commands");
    if(p.statuses[0].present && p.statuses[0].rt!=(p.rtToRt?p.second.rt:p.first.rt))return reject("First status does not match responding RT");
    if(p.statuses[1].present && (!p.rtToRt || p.broadcast || p.statuses[1].rt!=p.first.rt))return reject("Second status does not match receiving RT");
  }
  for(const auto &s:p.statuses)if(s.present) {
    if(s.rt==31 || (s.rt!=p.first.rt && (!p.rtToRt || s.rt!=p.second.rt)) || (p.broadcast && s.rt==p.first.rt))
      return reject("Unexpected responding RT address");
    if(s.fault())return reject("RT status reports fault or reserved bits");
    if(s.busy())return reject("RT reports busy; payload not usable");
  }
  if(r.flags)return reject("Capture protocol error");
  if(p.actualWords!=p.expectedWords)return reject("Captured payload count differs from command");
  if(r.source) {
    auto hasStatus=[&](unsigned rt){for(const auto &s:p.statuses)if(s.present&&s.rt==rt)return true;return false;};
    if((!p.broadcast&&!hasStatus(p.first.rt)) || (p.rtToRt&&!hasStatus(p.second.rt)))return reject("Expected status response absent");
  }
  p.valid=true;p.payloadEligible=p.expectedWords!=0;p.reason=p.payloadEligible?"Eligible":"No data payload";return p;
}
std::string statusDescription(const ProtocolInfo &p) {
  std::string out;
  constexpr std::pair<unsigned,std::string_view> bits[]={{0x400,"Message error"},{0x200,"Instrumentation"},{0x100,"Service request"},
    {0xe0,"Reserved status bits"},{0x10,"Broadcast received"},{8,"Busy"},{4,"Subsystem fault"},{2,"Dynamic bus control accepted"},{1,"Terminal fault"}};
  for(unsigned i=0;i<2;++i){if(!out.empty())out+="; ";const auto &s=p.statuses[i];out+="Status "+std::to_string(i+1)+": ";
    if(!s.present){out+="absent";continue;}out+="RT "+std::to_string(s.rt);bool any=false;
    for(auto [mask,name]:bits)if(s.bits&mask){out+=any?" | ":" ";out+=name;any=true;}if(!any)out+=" normal";}
  return out;
}
std::string protocolDiagnostics(const Record &r,const ProtocolInfo &p) {
  std::string out(p.reason);
  if(r.source) {
    constexpr std::pair<unsigned,std::string_view> errors[]={{1,"command error"},{2,"RT-to-RT second command error"},{4,"RT-to-RT address/subaddress error"},
      {8,"invalid word (parity/Manchester detail unavailable)"},{0x10,"invalid sync"},{0x20,"word count error"},{0x200,"no response"},{0x400,"format error"},{0x1000,"vendor error/superseding condition"}};
    for(auto [mask,name]:errors)if(r.vendorStatus&mask){out+="; ";out+=name;}
  }
  return out;
}
std::string_view protocolQuality(const Record &r,const ProtocolInfo &p,bool backwards) {
  if(backwards)return "CLOCK BACKWARD";
  if(!p.supported)return "UNSUPPORTED";
  if(!p.valid)return "WARN";
  if(r.validity&0x100)return "LEGACY TIME";
  return "OK";
}
}
