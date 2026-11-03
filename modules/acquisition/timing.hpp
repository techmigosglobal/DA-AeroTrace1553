#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <algorithm>
namespace aero {
// Fixed-memory log2 histogram. Quantiles are inclusive bucket upper bounds.
struct TimingHistogram {
  std::array<std::uint64_t,64> buckets{};std::uint64_t count{},totalNs{},maximumNs{};
  void observe(std::uint64_t ns){++buckets[std::min<unsigned>(63,std::bit_width(ns))];++count;totalNs+=ns;maximumNs=std::max(maximumNs,ns);}
  std::uint64_t quantile(unsigned percent) const {if(!count)return 0;const auto target=(count*percent+99)/100;std::uint64_t n=0;for(unsigned i=0;i<64;++i){n+=buckets[i];if(n>=target)return i==63?UINT64_MAX:(std::uint64_t(1)<<i)-1;}return maximumNs;}
};
enum TimingStage {SdkPoll,PacketProcessing,PacketWrite,RecordingWrite,Conversion,RtAggregation,Publication,Flush,WorkerBatch,SignalChecking,TimingStageCount};
inline constexpr const char *timingNames[]={"sdk_poll","packet_processing","raw_packet_write","recording_write","conversion","rt_aggregation","publication","flush","worker_batch","signal_checks"};
}
