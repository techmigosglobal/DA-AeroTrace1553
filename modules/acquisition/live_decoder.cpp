#include "live_decoder.hpp"
#include "protocol.hpp"
#include "field_encoding.hpp"
#include <set>
#include <limits>
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
void LiveDecoder::configure(std::vector<Signal> mappings,std::uint64_t version,bool example) {
  exampleContext=example;
  if(auto error=validateProfile(mappings);!error.empty()) throw std::invalid_argument(error);
  for(auto &signal:mappings)prepareConversion(signal);
  profile=std::move(mappings);revision=version;count=decodedValues=invalidValues=0;
  for(auto &route:routes) route.clear();
  values.assign(profile.size(),{});history.clear();traceSignals.clear();
  traceSlots.assign(profile.size(),-1);
  commandSelection={};commandHistory.clear();commandSlots.assign(profile.size(),-1);commandNewest=0;
  for(unsigned stream=0;stream<4;++stream) {sourceValues[stream].assign(profile.size(),{});sourceHistory[stream].clear();}
  selectTraces({0,1,2,3,4,5,6,7});
  for(unsigned index=0;index<profile.size();++index) {
    const auto &signal=profile[index];
    routes[(signal.rt<<6)|(signal.transmit<<5)|signal.sa].push_back(index);
  }
}
void LiveDecoder::selectCommandPlot(CommandPlotSelection selection) {
  if(selection.command!=commandSelection.command || selection.channel!=commandSelection.channel ||
     selection.bus!=commandSelection.bus) {
    commandHistory.clear();commandSlots.assign(profile.size(),-1);commandNewest=0;
    for(unsigned index=0;!selection.command.empty() && index<profile.size();++index) {
      if(signalCommandKey(profile[index])==selection.command) {
        commandSlots[index]=int(commandHistory.size());
        commandHistory.emplace_back();
      }
    }
  }
  commandSelection=std::move(selection);
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
  history=std::move(next);sourceHistory=std::move(sources);traceSignals=std::move(wanted);traceSlots.assign(profile.size(),-1);
  for(unsigned slot=0;slot<traceSignals.size();++slot) traceSlots[traceSignals[slot]]=int(slot);
}
void LiveDecoder::consume(std::span<const Record> records) {
  if(records.empty())return;
  // Cache encodings for the batch so busy commands do not repeatedly
  // validate field layout and inspect representation strings.
  std::vector<std::optional<FieldEncoding>> fastEncodings(profile.size());
  for(unsigned index=0;index<profile.size();++index)
    if(profile[index].sourceWord==SignalWord::Payload && profile[index].peerRt<0)
      fastEncodings[index].emplace(profile[index]);
  for(const auto &record:records) {
    ++count;
    const auto protocol=interpret(record);
    const auto key=[](const Command &c){return (c.rt<<6)|(c.transmit<<5)|c.sa;};
    const bool validStream=record.channel>=1 && record.channel<=2 && record.bus<2;
    const auto stream=validStream?(record.channel-1)*2+record.bus:0;
    const bool chartStream=record.channel==commandSelection.channel && record.bus==commandSelection.bus;
    const bool normalData=!protocol.rtToRt && !protocol.mode;
    const auto consumeRoute=[&](unsigned route) {
    for(const auto index:routes[route]) {
      const auto &signal=profile[index];
      const auto command=signalCommand(protocol,signal);
      if(key(command)!=route || !matchesSignalRoute(record,signal,command))continue;
      auto &latest=values[index];
      if(const auto &encoding=fastEncodings[index]; encoding && normalData) {
        if(signal.expectedWordCount &&
           (protocol.payloadCommand.count!=signal.expectedWordCount ||
            record.words.size()!=signal.expectedWordCount))continue;
        if(signal.word+encoding->wordCount>record.words.size()) {
          latest={};latest.timestampNs=record.timestampNs;latest.matched=true;
          latest.channel=record.channel;latest.bus=record.bus;
          latest.reason="Mapped data words unavailable";
        } else {
          latest={};
          latest.timestampNs=record.timestampNs;latest.matched=true;
          latest.channel=record.channel;latest.bus=record.bus;
          latest.reason=protocol.reason;
          const auto words=std::span(record.words).subspan(signal.word,encoding->wordCount);
          latest.rawCount=encoding->wordCount;
          std::copy(words.begin(),words.end(),latest.raw.begin());
          if(protocol.payloadEligible && (!signal.simulationOnly || (exampleContext && record.source==0))) {
            latest.value=encoding->convert(words);
            if(!latest.value)latest.reason="Invalid numeric value";
          }
        }
      } else latest=decodeValue(record,signal,protocol,exampleContext);
      if(latest.value) ++decodedValues;else ++invalidValues;
      if(validStream)sourceValues[stream][index]=latest;
      const auto commandSlot=commandSlots[index];
      if(commandSlot>=0 && chartStream) {
        commandHistory[commandSlot].append(record.timestampNs,latest.value);
        commandNewest=record.timestampNs/1e9;
      }
      const auto slot=traceSlots[index];
      if(slot>=0) {
        auto append=[&](auto &samples) {const auto seconds=record.timestampNs/1e9;if(!samples.empty() && seconds<samples.back().first) samples.emplace_back(seconds,std::numeric_limits<double>::quiet_NaN());samples.emplace_back(seconds,latest.value.value_or(std::numeric_limits<double>::quiet_NaN()));while(samples.size()>8192) samples.pop_front();};
        append(history[slot]);
        if(validStream)append(sourceHistory[stream][slot]);
      }
    }
    };
    consumeRoute(key(protocol.payloadCommand));
    if(key(protocol.first)!=key(protocol.payloadCommand))consumeRoute(key(protocol.first));
  }
}
LiveFrame LiveDecoder::snapshot(std::size_t displayPoints) const {
  LiveFrame frame;frame.revision=revision;frame.records=count;frame.values=values;
  frame.sourceValues=sourceValues;
  frame.traceSignals=traceSignals;
  frame.decodedValues=decodedValues;frame.invalidValues=invalidValues;
  for(unsigned index=0;index<std::min<std::size_t>(8,history.size());++index)
    frame.traces.push_back(downsample({history[index].begin(),history[index].end()},displayPoints));
  for(unsigned stream=0;stream<4;++stream) for(const auto &samples:sourceHistory[stream]) frame.sourceTraces[stream].emplace_back(samples.begin(),samples.end());
  if(!commandSelection.command.empty()) {
    auto &plot=frame.commandPlot.emplace();
    plot.selection=commandSelection;plot.newest=commandNewest;
    for(unsigned index=commandSelection.first;
        index<commandHistory.size() && index-commandSelection.first<std::min(24u,commandSelection.count);++index)
      plot.traces.push_back(commandHistory[index].points());
  }
  return frame;
}
}
