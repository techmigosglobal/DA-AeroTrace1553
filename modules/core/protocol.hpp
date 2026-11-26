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
  bool terminalBusy{}, terminalFault{}, terminalMessageError{}, reservedStatusBits{};
  bool sdkEvidenceAvailable{};
  unsigned expectedWords{}, actualWords{}, modeCode{};
  int transmitterRt{-1}, receiverRt{-1}; // -1 = BC/unspecified, 31 = broadcast receiver
  std::array<StatusInfo,2> statuses{};
  std::string_view transaction{"Unsupported"}, direction{"Unsupported"}, modeName{}, reason{"Unsupported message type"};
};
ProtocolInfo interpret(const Record &);
inline const Command &signalCommand(const ProtocolInfo &protocol,const Signal &signal) {
  return signal.sourceWord==SignalWord::Command1 || signal.sourceWord==SignalWord::Status2
    ? protocol.first : protocol.payloadCommand;
}
std::optional<std::uint16_t> capturedHeaderWord(const Record &,const ProtocolInfo &,SignalWord);
Record previewSignalRecord(const Signal &,std::span<const std::uint16_t>,unsigned channel,unsigned bus);
// Independent typed observations: one early interpret() rejection must not hide
// another issue on the same command. Unsupported SDK details stay unavailable.
struct ProtocolIssues {
  bool noResponseAvailable{}, noResponse{}, illegalMode{}, wrongTadr{}, invalid{};
};
ProtocolIssues protocolIssues(const Record &,const ProtocolInfo &);
std::optional<double> decodePayload(const Record &,const Signal &,const ProtocolInfo &,bool exampleContext=false);
std::string statusDescription(const ProtocolInfo &);
std::string protocolDiagnostics(const Record &, const ProtocolInfo &);
std::string_view protocolQuality(const Record &, const ProtocolInfo &, bool backwards=false);
}
