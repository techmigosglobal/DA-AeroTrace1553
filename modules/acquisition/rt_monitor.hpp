#pragma once
#include "protocol.hpp"
#include <array>
#include <map>

namespace aero {
struct RtBusSummary {
  std::uint64_t count{},noResponse{},illegalMode{},wrongTadr{},invalid{};
  // No-response block-status evidence is defined only for normalized DDC MT/MTI.
  // A mixed/unknown source cannot turn unavailable evidence into a displayed 0.
  bool noResponseKnown{true};
  Record latest;
};
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
  static constexpr std::string_view rulesRevision="rt-statistics-v1";
  std::uint64_t revision{};
  std::array<std::uint64_t,2> totalCommands{},invalidCommands{};
  std::array<std::map<unsigned,RtMonitorRow>,2> channels;
  void consume(const Record &r) {
    if(r.channel<1||r.channel>2||r.bus>1)return;
    const auto p=interpret(r);
    const auto issues=protocolIssues(r,p);
    ++totalCommands[r.channel-1];if(issues.invalid)++invalidCommands[r.channel-1];
    auto observe=[&](unsigned rt,const Command &command) {
      auto &row=channels[r.channel-1][rt];row.latest=r;
      auto &bus=row.buses[r.bus];++bus.count;bus.latest=r;
      bus.noResponseKnown&=issues.noResponseAvailable;
      bus.noResponse+=issues.noResponse;bus.illegalMode+=issues.illegalMode;
      bus.wrongTadr+=issues.wrongTadr;bus.invalid+=issues.invalid;
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
    // Preserve a valid first command even when an unsupported message type made
    // interpret() return before choosing the RT-to-RT payload command.
    const auto payload=p.rtToRt&&p.command2Present?p.second:p.first;
    observe(p.commandPresent?payload.rt:32,payload);
    if(p.commandPresent&&p.command2Present&&p.rtToRt&&p.first.rt!=payload.rt)observe(p.first.rt,p.first);
    ++revision;
  }
};
}
