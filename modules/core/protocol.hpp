#pragma once
#include "core.hpp"
#include <array>
#include <string_view>

namespace aero {
// Wire status masks, not the differently encoded SDK configuration masks.
struct StatusInfo {
  bool present{};
  unsigned rt{};
  std::uint16_t bits{};
  bool fault() const { return present && (bits & 0x04e5); }
  bool busy() const { return present && (bits & 8); }
};
struct ProtocolInfo {
  Command first{}, second{}, payloadCommand{};
  bool commandPresent{}, command2Present{}, supported{}, valid{}, payloadEligible{}, broadcast{}, rtToRt{}, mode{};
  unsigned expectedWords{}, actualWords{}, modeCode{};
  int transmitterRt{-1}, receiverRt{-1}; // -1 = BC/unspecified, 31 = broadcast receiver
  std::array<StatusInfo,2> statuses{};
  std::string_view transaction{"Unsupported"}, direction{"Unsupported"}, modeName{}, reason{"Unsupported message type"};
};
ProtocolInfo interpret(const Record &);
std::optional<double> decodePayload(const Record &,const Signal &,const ProtocolInfo &);
std::string statusDescription(const ProtocolInfo &);
std::string protocolDiagnostics(const Record &, const ProtocolInfo &);
std::string_view protocolQuality(const Record &, const ProtocolInfo &, bool backwards=false);
}
