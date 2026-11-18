#pragma once
#include "protocol.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace aero {
// These observations do not establish complete bus history or standards compliance.
struct SequenceObservation {
  std::uint64_t recordId{}, priorRecordId{};
  const char *kind{}, *evidence{};
};
class SequenceAnalysis {
  struct BusState {
    std::uint64_t lastTime{}, broadcastId{};
    bool seenTime{};
    std::array<std::uint64_t,31> controlOffer{};
  };
  std::array<BusState,4> buses{};
public:
  void reset() { buses={}; }
  // Call after any capture loss, discontinuity, overflow, disconnect or unknown ordering.
  void invalidate() { for(auto &bus:buses)bus=BusState{}; }
  std::vector<SequenceObservation> consume(const Record &r) {
    std::vector<SequenceObservation> out;
    if(r.channel<1 || r.channel>2 || r.bus>1)return out;
    auto &bus=buses[(r.channel-1)*2+r.bus];
    if((bus.seenTime && r.timestampNs<bus.lastTime) || (r.validity&0x100))bus=BusState{};
    bus.lastTime=r.timestampNs;bus.seenTime=true;
    const auto p=interpret(r);
    if(!p.supported || !p.valid || !p.sdkEvidenceAvailable){bus=BusState{};bus.seenTime=true;bus.lastTime=r.timestampNs;return out;}
    if(p.broadcast){bus.broadcastId=r.id;out.push_back({r.id,0,"broadcast","observed; receipt by individual RTs is not established"});}
    if(p.mode && !p.broadcast && p.first.rt<31){
      if(p.modeCode==0){bus.controlOffer[p.first.rt]=r.id;out.push_back({r.id,0,"dynamic_control_offer","observed in captured message"});}
      if(p.modeCode==18)out.push_back({r.id,0,"last_command_request","observed; previous command history may be incomplete"});
    }
    for(const auto &status:p.statuses)if(status.present && status.rt<31){
      if(status.bits&0x10)out.push_back({r.id,bus.broadcastId,"broadcast_status","reported by RT; association with a prior broadcast is not proven"});
      if(status.bits&0x02)out.push_back({r.id,bus.controlOffer[status.rt],"dynamic_control_status","acceptance bit observed; complete handover is not established"});
    }
    return out;
  }
};
}
