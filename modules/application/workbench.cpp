#include "workbench.hpp"
#include "record_csv.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include "downsample.hpp"

namespace aero {
bool Workbench::startRecording(const std::filesystem::path &path, std::array<bool,4> channels) {
  if (std::none_of(channels.begin(),channels.end(),[](bool on) { return on; })) return false;
  captureConfig.enabled=channels;
  if(!captureConfig.simulatorProfile.empty()) captureConfig.simulatorProfile=mappings();
  if(auto error=acquisition.setProfile(mappings());!error.empty()) return false;
  if (!acquisition.startConfigured(path,captureConfig)) return false;
  archive.clear(); replaySource=false; replayPlaying=false;
  return true;
}
void Workbench::stopRecording() {
  acquisition.stop(); accept(acquisition.drain());
}
bool Workbench::accept(const std::vector<Record> &incoming) {
  archive.insert(archive.end(),incoming.begin(),incoming.end());
  if(archive.size()<=200000) return false;
  archive.erase(archive.begin(),archive.begin()+(archive.size()-190000));
  return true;
}
ReadResult Workbench::loadRecording(const std::filesystem::path &path) {
  auto index=std::make_shared<RecordingIndex>();const auto status=index->open(path);
  ReadResult result;result.error=status.error;result.recovered=status.recovered;
  if(result.error.empty())useRecordingIndex(std::move(index));return result;
}
void Workbench::useRecordingIndex(std::shared_ptr<RecordingIndex> index) {
  recordingIndex=std::move(index);replaySource=true;replayPlaying=false;replayIndex=0;replayWindowStart=0;replayRecords.clear();
  selected={};seek(0);
}
bool Workbench::seek(std::size_t index) {
  if(!replayCount())return false;
  replayIndex=std::min(index,replayCount()-1);
  if(recordingIndex) {
    // Keep a bounded analysis window around the cursor; the index retains no payloads.
    if(replayRecords.empty() || replayIndex<replayWindowStart || replayIndex+7>=replayWindowStart+replayRecords.size()) {
      replayWindowStart=replayIndex>4096?replayIndex-4096:0;
      const auto finish=std::min(replayCount(),std::max(replayIndex+8,replayWindowStart+8192));replayRecords.clear();replayRecords.reserve(finish-replayWindowStart);
      for(auto i=replayWindowStart;i<finish;++i)replayRecords.push_back(recordingIndex->at(i));
    }
    selected=replayRecords.at(replayIndex-replayWindowStart);
  } else selected=replayRecords[replayIndex];
  replayPositionNs=double(recordingIndex?recordingIndex->playbackTimestamp(replayIndex):selected.timestampNs);return true;
}
void Workbench::advance(std::uint64_t milliseconds) {
  if(!replayPlaying || !replayCount())return;
  const auto target=replayPositionNs+milliseconds*1000000.*replaySpeed;
  auto lo=replayIndex,hi=replayCount();
  while(lo<hi){const auto mid=lo+(hi-lo)/2;if((recordingIndex?recordingIndex->playbackTimestamp(mid):replayTimestamp(mid))<=target)lo=mid+1;else hi=mid;}
  seek(lo?lo-1:0);replayPositionNs=target;
  if(replayIndex+1>=replayCount())replayPlaying=false;
}
const std::vector<Record> &Workbench::data(const std::vector<Record> &fallback) const {
  return replaySource ? replayRecords : archive.empty() ? fallback : archive;
}
std::size_t Workbench::end(const std::vector<Record> &fallback) const {
  return replaySource ? std::min(replayIndex-replayWindowStart+1,replayRecords.size()) : data(fallback).size();
}
std::optional<double> Workbench::latest(const Signal &mapping, const std::vector<Record> &fallback) const {
  if(!replaySource) if(auto live=acquisition.liveFrame()) {
    const auto active=mappings();
    for(unsigned index=0;index<active.size() && index<live->values.size();++index)
      if(active[index].id==mapping.id) return live->values[index].value;
  }
  if(!replaySource && acquisition.active()) return {};
  const auto &records=data(fallback);
  for(auto index=end(fallback);index>0;--index) {
    const auto &r=records[index-1];const auto c=interpret(r).payloadCommand;
    if(c.rt==mapping.rt&&c.sa==mapping.sa&&c.transmit==mapping.transmit&&(!mapping.channel||mapping.channel==r.channel)&&(mapping.bus<0||mapping.bus==r.bus))return decode(r,mapping);
  }
  return {};
}
std::vector<Signal> Workbench::demoSignals() {
  const char *names[]={"Sample Temperature","Hydraulic Pressure","Supply Voltage","Vibration Level","Motor Speed","Bus Current","Pitch Angle","Status Bits"};
  const char *units[]={"°C","kPa","V","g","RPM","A","deg","—"};
  std::vector<Signal> mappings;
  for(unsigned i=0;i<8;++i) {
    Signal mapping; mapping.id=names[i];mapping.unit=units[i];mapping.word=i;
    mapping.scale=i==4 ? .1 : i==7 ? 1 : .01;
    mappings.push_back(mapping);
  }
  return mappings;
}
std::vector<Signal> Workbench::mappings() const {
  if(!profile.empty()) return profile;
  if(replaySource) return {};
  if(captureConfig.hardware) return {};
  auto result=demoSignals();result[0]=signal;return result;
}
std::string Workbench::applyProfile(std::vector<Signal> mappings) {
  if(auto error=validateProfile(mappings);!error.empty()) return error;
  if(auto error=acquisition.setProfile(mappings);!error.empty()) return error;
  profile=std::move(mappings);if(!profile.empty()) signal=profile.front();return {};
}
std::vector<LiveValue> Workbench::liveValues(const std::vector<Record> &fallback,unsigned channel,int bus) const {
  if(!replaySource) if(auto frame=acquisition.liveFrame()) return channel>=1 && channel<=2 && bus>=0 && bus<2 ? frame->sourceValues[(channel-1)*2+bus] : frame->values;
  const auto profile=mappings();if(!replaySource && acquisition.active()) return std::vector<LiveValue>(profile.size());
  LiveDecoder decoder;decoder.configure(profile,0);const auto &records=data(fallback);const auto finish=end(fallback),begin=finish>8192 ? finish-8192 : 0;
  decoder.consume(std::span<const Record>(records).subspan(begin,finish-begin));const auto frame=decoder.snapshot();
  return channel>=1 && channel<=2 && bus>=0 && bus<2 ? frame.sourceValues[(channel-1)*2+bus] : frame.values;
}
LiveValue Workbench::liveValue(unsigned index,const std::vector<Record> &fallback,unsigned channel,int bus) const {
  const auto profile=mappings();if(index>=profile.size()) return {};
  if(!replaySource) if(auto live=acquisition.liveFrame()) {
    if(channel>=1 && channel<=2 && bus>=0 && bus<2) {const auto &values=live->sourceValues[(channel-1)*2+bus];return index<values.size() ? values[index] : LiveValue{};}
    return index<live->values.size() ? live->values[index] : LiveValue{};
  }
  if(!replaySource && acquisition.active()) return {};
  const auto &records=data(fallback);const auto &signal=profile[index];
  for(auto i=end(fallback);i>0;--i) {const auto &record=records[i-1];const auto cmd=interpret(record).payloadCommand;
    if((channel && record.channel!=channel)||(bus>=0 && record.bus!=bus)||(signal.channel && signal.channel!=record.channel)||(signal.bus>=0 && signal.bus!=record.bus)) continue;
    if(cmd.rt==signal.rt && cmd.sa==signal.sa && cmd.transmit==signal.transmit) return decodeValue(record,signal);
  }
  return {};
}
Series Workbench::series(const std::string &role,unsigned index,const std::vector<Record> &fallback,unsigned channel,int bus,int rawColumn) const {
  Series result;
  if(role=="replay" && !replaySource) return result;
  const auto &records=data(fallback); const auto finish=end(fallback);
  const auto begin=finish>5000 ? finish-5000 : 0;
  if(role=="raw_column") {
    std::array<std::optional<std::uint64_t>,4> previous;
    for(auto i=begin;i<finish;++i) {const auto &r=records[i];if((channel && r.channel!=channel)||(bus>=0 && r.bus!=bus)) continue;
      std::optional<double> value;
      if(rawColumn>=17 && rawColumn<49 && rawColumn-17<int(r.words.size())) value=r.words[rawColumn-17];
      else switch(rawColumn) {case 0:value=r.id;break;case 2:value=r.channel;break;case 3:value=r.bus;break;
        case 4:case 5:case 6:case 7:{const auto p=interpret(r);if(!p.supported||!p.commandPresent)break;const auto c=p.payloadCommand;if(rawColumn==4)value=c.rt;else if(rawColumn==5)value=c.sa;else if(rawColumn==6){if(!p.rtToRt&&!p.broadcast)value=c.transmit;}else if(!p.mode)value=c.count;break;}
        case 9:value=r.commandWord;break;case 10:value=r.flags;break;case 11:if(r.channel>=1 && r.channel<=2 && r.bus<2) {auto &prior=previous[(r.channel-1)*2+r.bus];if(prior && r.timestampNs>=*prior) value=(r.timestampNs-*prior)/1000.;prior=r.timestampNs;}break;case 12:value=r.vendorStatus;break;case 13:value=r.messageType;break;case 14:value=r.commandWord2;break;case 15:value=r.statusWord;break;case 16:value=r.statusWord2;break;}
      if(value) {const auto time=r.timestampNs/1e9;if(!result.primary.empty() && time<result.primary.back().first) result.primary.clear();result.primary.emplace_back(time,*value);}
    }
  } else if(role=="events") {
    std::map<std::uint64_t,unsigned> bins;
    for(auto i=begin;i<finish;++i) bins[records[i].timestampNs/1000000000]+=records[i].flags ? 1 : 0;
    for(const auto &[second,count]:bins) result.primary.emplace_back(second,count);
  } else if(role=="distribution") {
    std::array<unsigned,32> counts{};
    for(auto i=begin;i<finish;++i) ++counts[command(records[i].commandWord).rt];
    for(unsigned rt=0;rt<32;++rt) result.primary.emplace_back(rt,counts[rt]);
  } else if(role=="rate") {
    std::map<std::uint64_t,std::array<unsigned,2>> bins;
    for(auto i=begin;i<finish;++i) ++bins[records[i].timestampNs/1000000000][records[i].bus];
    for(const auto &[second,count]:bins) {
      result.primary.emplace_back(double(second),count[0]);
      result.secondary.emplace_back(double(second),count[1]);
    }
  } else {
    const auto mappings=this->mappings();
    if(index>=mappings.size()) return result;
    if(!replaySource) if(auto live=acquisition.liveFrame()) {
      const auto found=std::find(live->traceSignals.begin(),live->traceSignals.end(),index);
      if(found!=live->traceSignals.end()) {
        const auto slot=found-live->traceSignals.begin();
        if(channel>=1 && channel<=2 && bus>=0 && bus<2) {result.primary=live->sourceTraces[(channel-1)*2+bus][slot];return result;}
        if(!channel && bus<0) {result.primary=live->traces[slot];const auto next=std::find(live->traceSignals.begin(),live->traceSignals.end(),index+1);if(next!=live->traceSignals.end()) result.secondary=live->traces[next-live->traceSignals.begin()];return result;}
      }
    }
    for(auto i=begin;i<finish;++i) {
      if((channel && records[i].channel!=channel)||(bus>=0 && records[i].bus!=bus)) continue;
      const auto seconds=records[i].timestampNs/1e9;
      if(!result.primary.empty() && seconds<result.primary.back().first) {result.primary.clear();result.secondary.clear();}
      if(auto value=decode(records[i],mappings[index])) result.primary.emplace_back(seconds,*value);
      if(index+1<mappings.size()) if(auto value=decode(records[i],mappings[index+1])) result.secondary.emplace_back(seconds,*value);
    }
  }
  result.primary=downsample(result.primary);result.secondary=downsample(result.secondary);
  return result;
}
namespace {
std::string quotedCsv(const std::string &value) {
  std::string result="\"";
  for(char c:value) { result+=c; if(c=='"') result+='"'; }
  return result+'"';
}
std::string quotedJson(const std::string &value) {
  std::ostringstream out; out<<'"';
  for(unsigned char c:value) {
    switch(c) {
    case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
    case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
    default: if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
      else out<<char(c);
    }
  }
  out<<'"';return out.str();
}
std::string hexWord(unsigned word) {
  std::ostringstream out;out<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<word;return out.str();
}
}
ExportResult Workbench::exportData(ExportFormat format,std::uint64_t startMs,std::uint64_t endMs) const {
  if(replaySource && recordingIndex && recordingIndex->size()>replayRecords.size())return {{},"Use Save session CSVs to export the entire indexed recording",0};
  return exportRecords(replaySource ? replayRecords : archive,signal,format,startMs,endMs);
}
ExportResult Workbench::exportRecords(const std::vector<Record> &records, const Signal &signal,
    ExportFormat format,std::uint64_t startMs,std::uint64_t endMs) {
  ExportResult result;
  if(format!=ExportFormat::RawCsv && !validate(signal).empty()) {
    result.error="Invalid ICD mapping";return result;
  }
  if(startMs>endMs) { result.error="Invalid time range"; return result; }
  if(records.empty()) { result.error="No recorded data";return result; }
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(15);
  const auto hardware=std::count_if(records.begin(),records.end(),[](const auto &record){return record.source!=0;});
  const auto source=hardware==0 ? "SIMULATED" : hardware==records.size() ? "DDC_HARDWARE" : "MIXED";
  if(format==ExportFormat::RawCsv) out<<rawCsvHeader();
  else if(format==ExportFormat::EngineeringCsv) out<<"record,timestamp_ns,signal,value,unit,quality,source\n";
  else out<<"{\"source\":"<<quotedJson(source)<<",\"icd\":{\"schema\":\"aerotrace.icd.v1\",\"id\":"<<quotedJson(signal.id)
    <<",\"unit\":"<<quotedJson(signal.unit)<<",\"rt\":"<<signal.rt<<",\"sa\":"<<signal.sa<<",\"word\":"<<signal.word
    <<",\"lsb\":"<<signal.lsb<<",\"width\":"<<signal.width<<",\"signed\":"<<(signal.signedValue?"true":"false")
    <<",\"transmit\":"<<(signal.transmit?"true":"false")<<",\"scale\":"<<signal.scale<<",\"offset\":"<<signal.offset<<"},\"records\":[";
  for(const auto &record:records) {
    const auto ms=record.timestampNs/1000000;
    if(ms<startMs || ms>endMs) continue;
    if(format==ExportFormat::RawJson) {
      if(result.count) out<<',';
      out<<"{\"id\":"<<record.id<<",\"timestamp_ns\":\""<<record.timestampNs<<"\",\"channel\":"<<unsigned(record.channel)
         <<",\"bus\":\""<<(record.bus?'B':'A')<<"\",\"command\":"<<record.commandWord<<",\"status\":"<<record.statusWord
         <<",\"flags\":"<<record.flags<<",\"words\":[";
      for(std::size_t i=0;i<record.words.size();++i) { if(i) out<<',';out<<record.words[i]; }
      out<<"],\"source\":"<<quotedJson(record.source==2 ? "DDC_MT" : record.source ? "DDC_MTI" : "SIMULATED")<<",\"device_ticks\":\""<<record.deviceTicks
        <<"\",\"tick_resolution_ns\":"<<record.tickResolutionNs<<",\"vendor_status\":"<<record.vendorStatus
        <<",\"command2\":"<<record.commandWord2<<",\"status2\":"<<record.statusWord2<<",\"validity\":"<<record.validity
        <<",\"host_monotonic_ns\":\""<<record.hostMonotonicNs<<"\"}";
    } else if(format==ExportFormat::RawCsv) {
      out<<rawCsvRecord(record);
    } else {
      const auto value=decode(record,signal);
      out<<record.id<<','<<record.timestampNs<<','<<quotedCsv(signal.id)<<',';
      if(value) out<<*value;
      out<<','<<quotedCsv(signal.unit)<<','<<(value?"VALID":"NO_MATCH")<<','<<(record.source==2 ? "DDC_MT" : record.source ? "DDC_MTI" : "SIMULATED")<<'\n';
    }
    ++result.count;
  }
  if(format==ExportFormat::RawJson) out<<"]}\n";
  result.text=out.str(); return result;
}
ExportResult Workbench::exportProfileRecords(const std::vector<Record> &records,const std::vector<Signal> &profile,
    ExportFormat format,std::uint64_t startMs,std::uint64_t endMs) {
  if(format!=ExportFormat::EngineeringCsv) return exportRecords(records,profile.empty() ? Signal{"UNMAPPED",""} : profile.front(),format,startMs,endMs);
  ExportResult result;
  if(profile.empty()) {result.error="No active ICD mappings";return result;}
  if(auto error=validateProfile(profile);!error.empty()) {result.error=error;return result;}
  if(records.empty() || startMs>endMs) {result.error="No data or invalid time range";return result;}
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(15);
  out<<"record,timestamp_ns,signal,value,unit,quality,source,source_document,source_page\n";
  for(const auto &record:records) {
    const auto ms=record.timestampNs/1000000;if(ms<startMs || ms>endMs) continue;
    const auto command=interpret(record).payloadCommand;
    for(const auto &mapping:profile) {
      if(mapping.rt!=command.rt || mapping.sa!=command.sa || mapping.transmit!=command.transmit ||
          (mapping.channel && mapping.channel!=record.channel) || (mapping.bus>=0 && mapping.bus!=record.bus)) continue;
      const auto value=decode(record,mapping);
      out<<record.id<<','<<record.timestampNs<<','<<quotedCsv(mapping.id)<<',';
      if(value) out<<*value;
      out<<','<<quotedCsv(mapping.unit)<<','<<(value ? "VALID" : record.flags ? "FLAGGED" : "INVALID_PAYLOAD")
         <<','<<(record.source==2 ? "DDC_MT" : record.source ? "DDC_MTI" : "SIMULATED")<<','<<quotedCsv(mapping.sourceDocument)<<','<<mapping.sourcePage<<'\n';
      ++result.count;
    }
  }
  result.text=out.str();return result;
}
}
