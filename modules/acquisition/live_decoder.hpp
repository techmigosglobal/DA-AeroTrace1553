#pragma once
#include "downsample.hpp"
#include "core.hpp"
#include "protocol.hpp"
#include <array>
#include <deque>
#include <span>
namespace aero {
struct LiveValue {std::optional<double> value;std::uint64_t timestampNs{};bool matched{};unsigned channel{},bus{};std::array<std::uint16_t,4> raw{};unsigned rawCount{};std::string_view reason;};
inline LiveValue decodeValue(const Record &record,const Signal &signal,const ProtocolInfo &protocol) {
  LiveValue value{decodePayload(record,signal,protocol),record.timestampNs,true,record.channel,record.bus};
  value.reason=protocol.reason;
  if(!value.value && protocol.payloadEligible)value.reason="Mapping mismatch or invalid numeric value";
  for(unsigned i=0;i<4 && signal.word+i<record.words.size() && i<(signal.lsb+signal.width+15)/16;++i) value.raw[value.rawCount++]=record.words[signal.word+i];
  return value;
}
inline LiveValue decodeValue(const Record &record,const Signal &signal) {return decodeValue(record,signal,interpret(record));}
struct LiveFrame {
  std::uint64_t revision{}, records{};
  std::uint64_t decodedValues{},invalidValues{};
  std::vector<LiveValue> values;
  std::vector<std::vector<Sample>> traces;
  std::vector<unsigned> traceSignals;
  std::array<std::vector<LiveValue>,4> sourceValues;
  std::array<std::vector<std::vector<Sample>>,4> sourceTraces;
};
std::string validateProfile(const std::vector<Signal> &);
class LiveDecoder {
  std::vector<Signal> profile;
  std::array<std::vector<unsigned>,2048> routes;
  std::vector<LiveValue> values;
  std::vector<std::deque<Sample>> history;
  std::vector<unsigned> traceSignals;
  std::array<int,512> traceSlots;
  std::array<std::vector<LiveValue>,4> sourceValues;
  std::array<std::vector<std::deque<Sample>>,4> sourceHistory;
  std::uint64_t count{},revision{},decodedValues{},invalidValues{};
public:
  void configure(std::vector<Signal>,std::uint64_t);
  void consume(std::span<const Record>);
  void selectTraces(std::vector<unsigned>);
  LiveFrame snapshot(std::size_t displayPoints=1024) const;
  std::uint64_t conversions() const {return decodedValues;}
  std::uint64_t invalidConversions() const {return invalidValues;}
};
}
