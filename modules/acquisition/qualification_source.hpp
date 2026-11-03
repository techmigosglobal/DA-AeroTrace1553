#pragma once
#include "source.hpp"
#include <atomic>
#include <chrono>
#include <thread>
namespace aero {
inline Record qualificationRecord(std::uint64_t n){
  Record r;r.id=n+1;r.channel=1+(n%4)/2;r.bus=n%2;const unsigned rt=(n/4)%2?3:2;
  r.commandWord=(rt<<11)|(2<<5)|8;r.statusWord=rt<<11;r.validity=5;r.tickResolutionNs=2000;r.deviceTicks=(n/4)*40;r.timestampNs=r.deviceTicks*2000;
  const auto sequence=n/4;r.words={std::uint16_t(sequence),std::uint16_t(sequence>>16),std::uint16_t(sequence>>32),std::uint16_t((r.channel<<8)|r.bus),0x1234,0x5678,std::uint16_t(sequence^0xa55a),8};return r;
}
class QualificationSource final:public CaptureSource {
  std::chrono::steady_clock::time_point began;std::uint64_t next{};unsigned rate;std::shared_ptr<std::atomic<std::uint64_t>> generated;
public:
  QualificationSource(unsigned rate,std::shared_ptr<std::atomic<std::uint64_t>> count):rate(rate),generated(std::move(count)){}
  std::string open(const CaptureConfig &)override{next=0;return {};}
  std::string startMonitoring()override{began=std::chrono::steady_clock::now();next=0;*generated=0;return {};}
  std::string poll(SourceBatch &b,std::stop_token stop)override{
    using namespace std::chrono;const auto now=steady_clock::now();const auto ns=duration_cast<nanoseconds>(now-began).count();const auto target=std::uint64_t(ns/1000000000)*rate+std::uint64_t(ns%1000000000)*rate/1000000000+1;
    for(unsigned count=0;next<target&&count<1024&&!stop.stop_requested();++count){auto r=qualificationRecord(next++);r.hostMonotonicNs=duration_cast<nanoseconds>(now.time_since_epoch()).count();b.records.push_back(std::move(r));}
    *generated=next;if(b.records.empty())std::this_thread::sleep_for(milliseconds(1));return {};
  }
  void close()override{}
};
}
