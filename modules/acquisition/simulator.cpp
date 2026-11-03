#include "source.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <bit>
#include <cmath>
#include <limits>
#include <map>

namespace aero {
std::string validateSimulation(const CaptureConfig &c) {
  if(c.simulatorRate<1 || c.simulatorRate>100000) return "Simulator rate must be 1–100000 records/s";
  if(c.simulatorWave!="sine" && c.simulatorWave!="square" && c.simulatorWave!="ramp" && c.simulatorWave!="constant" && c.simulatorWave!="noise") return "Unknown simulator waveform";
  if(!std::isfinite(c.simulatorAmplitude) || c.simulatorAmplitude<0 || c.simulatorAmplitude>1e12 || !std::isfinite(c.simulatorOffset) || std::abs(c.simulatorOffset)>1e12 || !std::isfinite(c.simulatorFrequency) || c.simulatorFrequency<=0 || c.simulatorFrequency>10000) return "Invalid simulator amplitude, offset or frequency";
  if(c.simulatorProfile.size()>512) return "Maximum 512 simulation mappings";
  for(const auto &s:c.simulatorProfile) {
    if(auto e=validate(s);!e.empty()) return e;
    if(s.sa==0 || s.sa==31 || s.scale==0) return "Simulation requires data subaddresses and nonzero mapping scales";
    bool enabled=false;for(unsigned k=0;k<4;++k) enabled|=c.enabled[k] && (!s.channel || s.channel==k/2+1) && (s.bus<0 || s.bus==int(k%2));
    if(!enabled) return "Simulation mapping has no enabled stream: "+s.id;
  }
  std::map<unsigned,std::array<std::uint16_t,32>> occupied;
  for(const auto &s:c.simulatorProfile) {
    std::array<std::uint16_t,32> mask{};const auto words=(s.lsb+s.width+15)/16;
    for(unsigned p=s.lsb;p<s.lsb+s.width;++p)mask[s.word+(s.leastSignificantWordFirst?p/16:words-1-p/16)]|=std::uint16_t(1u<<(p%16));
    for(unsigned k=0;k<4;++k)if(c.enabled[k] && (!s.channel || s.channel==k/2+1) && (s.bus<0 || s.bus==int(k%2))) {
      auto &used=occupied[(k<<11)|(s.rt<<6)|(unsigned(s.transmit)<<5)|s.sa];for(unsigned w=0;w<32;++w){if(used[w]&mask[w])return "Simulation requires non-overlapping fields: "+s.id;used[w]|=mask[w];}
    }
  }
  return {};
}
Record simulationRecord(const CaptureConfig &c,std::uint64_t n) {
  if(!c.simulatorRate || n/c.simulatorRate>UINT64_MAX/1000000000)throw std::invalid_argument("Simulation time exceeds the supported range");
  const auto timestamp=(n/c.simulatorRate)*1000000000+(n%c.simulatorRate)*1000000000/c.simulatorRate;
  if(c.simulatorProfile.empty()) {auto r=synthetic(n);if(n%16<4) {r.commandWord=(3<<11)|(2<<5)|8;r.statusWord=3<<11;}r.timestampNs=timestamp;if(c.simulatorFlagEvery)r.flags=(n+1)%c.simulatorFlagEvery==0?1:0;return r;}
  const auto &route=c.simulatorProfile[n%c.simulatorProfile.size()];
  std::vector<unsigned> paths;for(unsigned k=0;k<4;++k) if(c.enabled[k] && (!route.channel || route.channel==k/2+1) && (route.bus<0 || route.bus==int(k%2))) paths.push_back(k);
  if(paths.empty()) throw std::invalid_argument("Simulation mapping has no enabled stream");
  const auto path=paths[(n/c.simulatorProfile.size())%paths.size()];
  Record r;r.id=n+1;r.timestampNs=timestamp;r.channel=path/2+1;r.bus=path%2;r.source=0;
  r.commandWord=(route.rt<<11)|(unsigned(route.transmit)<<10)|(route.sa<<5);r.statusWord=route.rt<<11;r.words.resize(32);
  const auto phase=std::fmod(double(n)/c.simulatorRate*c.simulatorFrequency,1.0);
  double wave=0;
  if(c.simulatorWave=="sine") wave=std::sin(phase*6.283185307179586);
  else if(c.simulatorWave=="square") wave=phase<.5 ? 1 : -1;
  else if(c.simulatorWave=="ramp") wave=2*phase-1;
  else if(c.simulatorWave=="noise") {auto x=n+c.simulatorSeed+0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;wave=double((x^(x>>31))>>11)/9007199254740992.0*2-1;}
  for(const auto &s:c.simulatorProfile) {
  if(s.rt!=route.rt || s.sa!=route.sa || s.transmit!=route.transmit || (s.channel && s.channel!=r.channel) || (s.bus>=0 && s.bus!=r.bus))continue;
  const long double raw=(static_cast<long double>(c.simulatorOffset)+static_cast<long double>(c.simulatorAmplitude)*wave-s.offset)/s.scale;
  std::uint64_t bits{};
  if(s.numericType=="float32") bits=std::bit_cast<std::uint32_t>(float(std::clamp(raw,-static_cast<long double>(std::numeric_limits<float>::max()),static_cast<long double>(std::numeric_limits<float>::max()))));
  else if(s.numericType=="float64") bits=std::bit_cast<std::uint64_t>(double(std::clamp(raw,-static_cast<long double>(std::numeric_limits<double>::max()),static_cast<long double>(std::numeric_limits<double>::max()))));
  else if(s.numericType=="bcd") {
    const auto max=std::pow(10.L,s.width/4)-1;auto value=std::uint64_t(std::clamp(std::round(raw),0.L,max));
    for(unsigned i=0;i<s.width/4;++i) {bits|=(value%10)<<(i*4);value/=10;}
  } else if(s.signedValue) {
    const auto lo=-std::ldexp(1.L,s.width-1),hi=std::ldexp(1.L,s.width-1)-1;
    bits=std::bit_cast<std::uint64_t>(std::int64_t(std::clamp(std::round(raw),lo,hi)));
  } else {
    const auto hi=s.width==64 ? static_cast<long double>(UINT64_MAX) : std::ldexp(1.L,s.width)-1;
    bits=std::uint64_t(std::clamp(std::round(raw),0.L,hi));
  }
  if(s.width<64) bits&=(std::uint64_t(1)<<s.width)-1;bits<<=s.lsb;
  const auto count=(s.width+s.lsb+15)/16;
  for(unsigned i=0;i<count;++i) r.words[s.word+(s.leastSignificantWordFirst ? i : count-1-i)]|=std::uint16_t(bits>>(i*16));
  }
  if(c.simulatorFlagEvery && (n+1)%c.simulatorFlagEvery==0) r.flags=1;
  return r;
}
namespace {
class Simulator final : public CaptureSource {
  CaptureConfig config;
  std::chrono::steady_clock::time_point start;
  std::uint64_t next{};
public:
  std::string open(const CaptureConfig &value) override {
    config=value;start=std::chrono::steady_clock::now();next=0;
    if(auto error=validateSimulation(config);!error.empty()) return error;
    return {};
  }
  std::string poll(SourceBatch &batch,std::stop_token stop) override {
    using namespace std::chrono;
    const auto elapsed=duration_cast<nanoseconds>(steady_clock::now()-start).count();
    const auto target=std::uint64_t(elapsed/1000000000)*config.simulatorRate+std::uint64_t(elapsed%1000000000)*config.simulatorRate/1000000000+1;
    for(unsigned count=0;next<target && count<1024 && !stop.stop_requested();++count) {
      auto record=simulationRecord(config,next);
      ++next;
      if(config.simulatorDropEvery && next%config.simulatorDropEvery==0) continue;
      if(config.enabled[(record.channel-1)*2+record.bus]) batch.records.push_back(std::move(record));
    }
    if(batch.records.empty()) std::this_thread::sleep_for(milliseconds(1));
    return {};
  }
  void close() override {}
};
}
std::unique_ptr<CaptureSource> makeSimulator() { return std::make_unique<Simulator>(); }
}
