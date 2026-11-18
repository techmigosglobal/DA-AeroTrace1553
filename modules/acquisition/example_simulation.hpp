#pragma once
#include "core.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace aero {
inline constexpr char offlineFixtureId[]="offline_rt03_rt08_v1";
// Synthetic engineering demonstration only; not a verified device ICD.
inline Record exampleSimulationRecord(std::uint64_t index,unsigned rate,unsigned stream) {
  if(rate==0 || stream>3 || index/rate>UINT64_MAX/1000000000ull)throw std::invalid_argument("Invalid example simulation time or stream");
  Record r;r.id=index+1;r.channel=1+stream/2;r.bus=stream%2;r.source=0;
  r.timestampNs=(index/rate)*1000000000ull+(index%rate)*1000000000ull/rate;
  r.commandWord=(3u<<11)|(12u<<5);r.statusWord=3u<<11;r.words.assign(32,0);
  const auto wave=std::sin(double(index)/double(rate));
  auto put=[&](unsigned first,unsigned n,std::uint64_t bits){for(unsigned j=0;j<n;++j)r.words[first+j]=std::uint16_t(bits>>(16*(n-j-1)));};
  for(unsigned i=0;i<3;++i)put(i*2,2,std::bit_cast<std::uint32_t>(float(100+20*i+10*wave)));
  for(unsigned i=0;i<3;++i)r.words[6+i]=std::uint16_t(std::int16_t(10*i+5*wave));
  for(unsigned i=0;i<3;++i)put(9+i*4,4,std::bit_cast<std::uint64_t>(double(i+1)+wave));
  for(unsigned i=21;i<27;++i)r.words[i]=std::uint16_t(index+i);
  for(unsigned i=27;i<30;++i)r.words[i]=((index/(std::uint64_t(i-26)*rate))&1)?0xffff:0;
  return r;
}
inline std::vector<Signal> exampleSimulationProfile() {
  std::vector<Signal> fields;
  auto field=[&](unsigned word,unsigned width,const char *kind,const char *unit,bool sign){
    Signal s;s.id="Demo word "+std::to_string(word+1)+(width>16?"–"+std::to_string(word+width/16):"");
    s.unit=unit;s.rt=3;s.sa=12;s.transmit=false;s.word=word;s.width=width;s.scale=1;s.offset=0;
    s.signedValue=sign;s.numericType=kind;s.expectedWordCount=32;s.simulationOnly=true;
    s.operatorNote="SIMULATION ONLY: assumed IEEE 754, MSW first, 16-bit integers, identity scaling";fields.push_back(s);
  };
  for(unsigned w:{0u,2u,4u})field(w,32,"float32","km/hr",false);
  for(unsigned w:{6u,7u,8u})field(w,16,"integer","degree",true);
  for(unsigned w:{9u,13u,17u})field(w,64,"float64","m/s²",false);
  for(unsigned w=21;w<=29;++w)field(w,16,"integer",w<=23?"m":w>=27?"s":"",w>=24&&w<=26);
  for(unsigned word=27;word<=29;++word)for(unsigned bit=4;bit<=19;++bit){
    Signal s;s.id="Demo word "+std::to_string(word+1)+" · 1553 bit "+std::to_string(bit);
    s.unit="state";s.rt=3;s.sa=12;s.word=word;s.width=1;s.lsb=19-bit;s.scale=1;s.offset=0;s.expectedWordCount=32;s.simulationOnly=true;
    s.operatorNote="SIMULATION ONLY: raw unsigned short bit; bit 4 is MSB";fields.push_back(s);
  }
  return fields;
}
}
