#include "live_decoder.hpp"
#include "protocol.hpp"
#include <set>
#include <stdexcept>
namespace aero {
std::string validateProfile(const std::vector<Signal> &profile) {
  if(profile.size()>512) return "Maximum 512 active ICD signals";
  std::set<std::string> ids;
  for(const auto &signal:profile) {
    if(auto error=validate(signal);!error.empty()) return signal.id+": "+error;
    if(!ids.insert(signal.id).second) return "Duplicate signal ID: "+signal.id;
  }
  // Overlapping fields can be intentional aliases. They remain separate named
  // mappings; conflicting duplicate IDs above must be resolved before activation.
  return {};
}
void LiveDecoder::configure(std::vector<Signal> mappings,std::uint64_t version) {
  if(auto error=validateProfile(mappings);!error.empty()) throw std::invalid_argument(error);
  profile=std::move(mappings);revision=version;count=decodedValues=invalidValues=0;
  for(auto &route:routes) route.clear();
  values.assign(profile.size(),{});history.clear();traceSignals.clear();
  for(unsigned stream=0;stream<4;++stream) {sourceValues[stream].assign(profile.size(),{});sourceHistory[stream].clear();}
  selectTraces({0,1,2,3,4,5,6,7});
  for(unsigned index=0;index<profile.size();++index) {
    const auto &signal=profile[index];
    routes[(signal.rt<<6)|(signal.transmit<<5)|signal.sa].push_back(index);
  }
}
void LiveDecoder::selectTraces(std::vector<unsigned> selected) {
  std::vector<unsigned> wanted;for(auto index:selected) if(index<profile.size() && wanted.size()<8 && std::find(wanted.begin(),wanted.end(),index)==wanted.end()) wanted.push_back(index);
  std::vector<std::deque<Sample>> next(wanted.size());std::array<std::vector<std::deque<Sample>>,4> sources;
  for(auto &source:sources) source.resize(wanted.size());
  for(unsigned slot=0;slot<wanted.size();++slot) {
    const auto found=std::find(traceSignals.begin(),traceSignals.end(),wanted[slot]);
    if(found!=traceSignals.end()) {const auto old=found-traceSignals.begin();next[slot]=std::move(history[old]);for(unsigned s=0;s<4;++s) sources[s][slot]=std::move(sourceHistory[s][old]);}
    else {
      const auto &value=values[wanted[slot]];if(value.value) next[slot].emplace_back(value.timestampNs/1e9,*value.value);
      for(unsigned s=0;s<4;++s) {const auto &value=sourceValues[s][wanted[slot]];if(value.value) sources[s][slot].emplace_back(value.timestampNs/1e9,*value.value);}
    }
  }
  history=std::move(next);sourceHistory=std::move(sources);traceSignals=std::move(wanted);traceSlots.fill(-1);
  for(unsigned slot=0;slot<traceSignals.size();++slot) traceSlots[traceSignals[slot]]=int(slot);
}
void LiveDecoder::consume(std::span<const Record> records) {
  for(const auto &record:records) {
    ++count;
    const auto protocol=interpret(record);const auto command=protocol.payloadCommand;
    for(const auto index:routes[(command.rt<<6)|(command.transmit<<5)|command.sa]) {
      const auto &signal=profile[index];
      if((signal.channel && signal.channel!=record.channel) || (signal.bus>=0 && signal.bus!=record.bus)) continue;
      auto &latest=values[index];latest=decodeValue(record,signal,protocol);
      ++decodedValues;if(!latest.value) ++invalidValues;
      const auto stream=(record.channel-1)*2+record.bus;
      if(record.channel>=1 && record.channel<=2 && record.bus<2) sourceValues[stream][index]=latest;
      const auto slot=traceSlots[index];
      if(latest.value && slot>=0) {
        auto append=[&](auto &samples) {const auto seconds=record.timestampNs/1e9;if(!samples.empty() && seconds<samples.back().first) samples.clear();samples.emplace_back(seconds,*latest.value);if(samples.size()>8192) samples.pop_front();};
        append(history[slot]);
        if(record.channel>=1 && record.channel<=2 && record.bus<2) append(sourceHistory[stream][slot]);
      }
    }
  }
}
LiveFrame LiveDecoder::snapshot(std::size_t displayPoints) const {
  LiveFrame frame;frame.revision=revision;frame.records=count;frame.values=values;
  frame.sourceValues=sourceValues;
  frame.traceSignals=traceSignals;
  frame.decodedValues=decodedValues;frame.invalidValues=invalidValues;
  for(unsigned index=0;index<std::min<std::size_t>(8,history.size());++index)
    frame.traces.push_back(downsample({history[index].begin(),history[index].end()},displayPoints));
  for(unsigned stream=0;stream<4;++stream) for(const auto &samples:sourceHistory[stream]) frame.sourceTraces[stream].push_back(downsample({samples.begin(),samples.end()},displayPoints));
  return frame;
}
}
