#pragma once
#include "core.hpp"
#include "protocol.hpp"
#include <array>
namespace aero {
struct RtObservation { std::uint64_t responses{}, lastNs{}; unsigned buses{}; };
struct RtActivity {
  std::array<std::array<RtObservation,31>,2> channels{};
  std::array<std::uint64_t,2> broadcasts{};
  void observe(const Record &r) {
    if(r.channel<1 || r.channel>2 || r.bus>1)return;
    const auto p=interpret(r);const auto c=p.first,c2=p.second;const auto ch=r.channel-1;
    if(c.broadcast && p.commandPresent)++broadcasts[ch];
    // Hardware flag bits explicitly identify present command/status words. A zero
    // status is a legitimate RT0 response, so presence cannot be inferred by value.
    if(r.flags || !p.supported || !p.commandPresent)return;
    auto response=[&](std::uint16_t status,bool present) {
      if(!present)return;const unsigned rt=status>>11;if(rt>=31)return;
      if(rt!=c.rt && (!(r.validity&2) || rt!=c2.rt))return;
      if(rt==c.rt && c.broadcast)return;
      auto &v=channels[ch][rt];++v.responses;v.lastNs=r.timestampNs;v.buses|=1u<<r.bus;
    };
    response(r.statusWord,p.statuses[0].present);
    response(r.statusWord2,p.statuses[1].present && p.command2Present);
  }
};
}
