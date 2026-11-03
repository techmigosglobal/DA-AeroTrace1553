#pragma once
#include "protocol.hpp"
#include <array>
#include <map>

namespace aero {
struct RtBusSummary {std::uint64_t count{};Record latest;};
struct RtRouteSummary {unsigned sa{},bus{};bool transmit{},mode{};std::uint64_t count{};Record latest;};
struct RtMonitorRow {
  std::array<RtBusSummary,2> buses;
  Record latest;
  std::map<std::uint64_t,RtRouteSummary> routes;
  std::uint64_t unlistedRouteMessages{};
};
// Receive-side aggregate. Independent of profile changes, display filters and
// the bounded raw display queue. IDs 31=broadcast, 32=unclassified.
struct RtMonitorFrame {
  std::string session;
  std::uint64_t revision{};
  std::array<std::map<unsigned,RtMonitorRow>,2> channels;
  void consume(const Record &r) {
    if(r.channel<1||r.channel>2||r.bus>1)return;
    const auto p=interpret(r);
    auto observe=[&](unsigned rt,const Command &command) {
      auto &row=channels[r.channel-1][rt];row.latest=r;
      auto &bus=row.buses[r.bus];++bus.count;bus.latest=r;
      const unsigned address=command.mode?command.count:command.sa;
      const std::uint64_t key=(std::uint64_t(r.bus)<<32)|(std::uint64_t(address)<<24)|
        (std::uint64_t(command.transmit)<<23)|(std::uint64_t(command.mode)<<22)|r.messageType;
      auto found=row.routes.find(key);
      if(found==row.routes.end()) {
        if(row.routes.size()>=128){++row.unlistedRouteMessages;return;}
        found=row.routes.emplace(key,RtRouteSummary{address,r.bus,command.transmit,command.mode}).first;
      }
      ++found->second.count;found->second.latest=r;
    };
    observe(p.commandPresent?p.payloadCommand.rt:32,p.payloadCommand);
    if(p.commandPresent&&p.command2Present&&p.rtToRt&&p.first.rt!=p.payloadCommand.rt)observe(p.first.rt,p.first);
    ++revision;
  }
};
}
