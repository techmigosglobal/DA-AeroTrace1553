#pragma once
#include "downsample.hpp"
#include "core.hpp"
#include "protocol.hpp"
#include "command_plot.hpp"
#include <array>
#include <deque>
#include <span>
namespace aero {
struct LiveValue {std::optional<double> value;std::uint64_t timestampNs{};bool matched{};unsigned channel{},bus{};std::array<std::uint16_t,4> raw{};unsigned rawCount{};std::string_view reason;};
inline LiveValue decodeValue(const Record &record,const Signal &signal,const ProtocolInfo &protocol, bool exampleContext=false) {
  if(!matchesSignalRoute(record,signal,signalCommand(protocol,signal))) return {};
  LiveValue value{decodePayload(record,signal,protocol,exampleContext),record.timestampNs,true,record.channel,record.bus};
  value.reason=protocol.reason;
  if(signal.sourceWord!=SignalWord::Payload) {
    if(const auto word=capturedHeaderWord(record,protocol,signal.sourceWord))value.raw[value.rawCount++]=*word;
    else value.reason="Mapped command or status word unavailable";
    if(value.value)value.reason="Mapped word decoded";
    return value;
  }
  if(!value.value && protocol.payloadEligible) {
    if(signal.simulationOnly && (!exampleContext || record.source!=0)) value.reason="Simulation-only profile · verified Offline simulation required";
    else if(signal.expectedWordCount && (protocol.mode || protocol.payloadCommand.count!=signal.expectedWordCount || record.words.size()!=signal.expectedWordCount))
      value.reason="Expected word count mismatch";
    else if(signal.word+(signal.lsb+signal.width+15)/16>record.words.size())value.reason="Mapped data words unavailable";
    else value.reason="Mapping mismatch or invalid numeric value";
  }
  for(unsigned i=0;i<4 && signal.word+i<record.words.size() && i<(signal.lsb+signal.width+15)/16;++i) value.raw[value.rawCount++]=record.words[signal.word+i];
  return value;
}
inline LiveValue decodeValue(const Record &record,const Signal &signal, bool exampleContext=false) {return decodeValue(record,signal,interpret(record),exampleContext);}
struct LiveFrame {
  std::uint64_t revision{}, records{};
  std::uint64_t decodedValues{},invalidValues{};
  std::vector<LiveValue> values;
  std::vector<std::vector<Sample>> traces;
  std::vector<unsigned> traceSignals;
  std::array<std::vector<LiveValue>,4> sourceValues;
  std::array<std::vector<std::vector<Sample>>,4> sourceTraces;
  std::optional<CommandPlotFrame> commandPlot;
};
inline std::string signalCommandKey(const Signal &signal) {
  if(!signal.messageId.empty()) return signal.messageId;
  std::string key="rt"+std::to_string(signal.rt)+"_sa"+std::to_string(signal.sa)+
    (signal.transmit?"_tx":"_rx");
  if(signal.expectedWordCount) key+="_wc"+std::to_string(signal.expectedWordCount);
  if(signal.matchWord>=0) key+="_w"+std::to_string(signal.matchWord)+"_"+std::to_string(signal.matchValue);
  return key;
}
std::string validateProfile(const std::vector<Signal> &);
class LiveDecoder {
  bool exampleContext{};
  std::vector<Signal> profile;
  std::array<std::vector<unsigned>,2048> routes;
  std::vector<LiveValue> values;
  std::vector<std::deque<Sample>> history;
  std::vector<unsigned> traceSignals;
  std::vector<int> traceSlots;
  std::array<std::vector<LiveValue>,4> sourceValues;
  std::array<std::vector<std::deque<Sample>>,4> sourceHistory;
  CommandPlotSelection commandSelection;
  std::vector<CommandTrace> commandHistory;
  std::vector<int> commandSlots;
  double commandNewest{};
  std::uint64_t count{},revision{},decodedValues{},invalidValues{};
public:
  void configure(std::vector<Signal>,std::uint64_t,bool example=false);
  void consume(std::span<const Record>);
  void selectTraces(std::vector<unsigned>);
  void selectCommandPlot(CommandPlotSelection);
  LiveFrame snapshot(std::size_t displayPoints=1024) const;
  std::uint64_t conversions() const {return decodedValues;}
  std::uint64_t invalidConversions() const {return invalidValues;}
};
}
