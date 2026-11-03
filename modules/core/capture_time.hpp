#pragma once
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
namespace aero {
// Relative card time, not local wall time. Hours never wrap at midnight.
inline std::string formatCardTime(std::uint64_t ns) {
  const auto us=ns/1000, seconds=us/1000000;
  std::ostringstream out;out<<std::setfill('0')<<std::setw(2)<<seconds/3600<<':'
    <<std::setw(2)<<(seconds/60)%60<<':'<<std::setw(2)<<seconds%60<<'.'
    <<std::setw(3)<<(us/1000)%1000<<'.'<<std::setw(3)<<us%1000;
  return out.str();
}
struct ClockStats {
  std::uint64_t count{},firstNs{},lastNs{},backwards{},repeated{},maxForwardGapNs{};
  void observe(std::uint64_t ns) {
    if(!count) firstNs=ns;
    else if(ns<lastNs) ++backwards;
    else if(ns==lastNs) ++repeated;
    else if(ns-lastNs>maxForwardGapNs) maxForwardGapNs=ns-lastNs;
    lastNs=ns;++count;
  }
};
struct RelativeClock48 {
  static constexpr std::uint64_t range=1ull<<48,mask=range-1;
  std::uint64_t latest{},wraps{};bool seen{};
  std::uint64_t extend(std::uint64_t raw) {
    auto value=(latest&~mask)|(raw&mask);
    if(seen && value<latest && latest-value>range/2) {value+=range;++wraps;}
    else if(seen && value>latest && value-latest>range/2 && value>=range) value-=range;
    if(!seen || value>latest) latest=value;seen=true;return value;
  }
};
inline std::string jsonString(const std::string &value) {
  std::ostringstream out;out<<'"';
  for(unsigned char c:value) {
    if(c=='"'||c=='\\') out<<'\\'<<c;
    else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
    else out<<char(c);
  }
  return out.str()+'"';
}
}
