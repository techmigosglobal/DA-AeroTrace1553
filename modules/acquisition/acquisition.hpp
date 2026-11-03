#pragma once
#include "source.hpp"
#include "live_decoder.hpp"
#include "capture_time.hpp"
#include "rt_activity.hpp"
#include "rt_monitor.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <future>

namespace aero {
struct CaptureMetrics {
  std::uint64_t recorded{}, displayDrops{}, hardwareLost{}, hardwareOverflows{}, batches{};
  std::size_t queueDepth{}, queueHighWater{};
  double maxBatchMilliseconds{};
  std::uint64_t decodedValues{},invalidValues{};
  std::array<std::uint64_t,4> sourceCounts{};
  std::array<std::uint64_t,2> channelLost{},channelOverflows{};
  std::array<DeviceState,2> devices;
  std::array<ClockStats,4> clocks;
  RtActivity rtActivity;
  bool hardware{}, failed{};
  std::string error;
  std::uint64_t checkCount{};
  std::vector<CheckEvent> checkEvents; // Most recent 512; complete log remains on disk.
};
// One I/O owner: SDK handles, original packets and recorder. The GUI only
// consumes bounded batches. Display backpressure never drops raw disk records.
class Acquisition {
  mutable std::mutex mutex;
  std::jthread worker;
  std::deque<Record> pending;
  CaptureMetrics state;
  std::vector<Signal> requestedProfile;
  std::uint64_t profileRevision{};
  std::vector<unsigned> requestedTraces{0,1,2,3,4,5,6,7};
  std::uint64_t traceRevision{};
  std::shared_ptr<const LiveFrame> live;
  std::shared_ptr<const RtMonitorFrame> rtLive;
  std::atomic<bool> running{}, connectedFlag{}, retainConnection{};
  std::unique_ptr<CaptureSource> retainedSource;
  CaptureConfig retainedConfig;
public:
  Acquisition()=default;
  Acquisition(const Acquisition &)=delete;
  Acquisition &operator=(const Acquisition &)=delete;
  ~Acquisition();
  bool start(const std::filesystem::path &,std::array<bool,4> enabled={true,true,true,true});
  bool startConfigured(const std::filesystem::path &,CaptureConfig,std::unique_ptr<CaptureSource> source={});
  std::future<bool> beginConfigured(const std::filesystem::path &,CaptureConfig,std::unique_ptr<CaptureSource> source={});
  bool connectConfigured(CaptureConfig,std::unique_ptr<CaptureSource> source={});
  std::future<bool> beginConnection(CaptureConfig,std::unique_ptr<CaptureSource> source={});
  bool connected() const;
  void stopRecording();
  void requestStop();
  void stop();
  std::vector<Record> drain(std::size_t maximum=8192);
  bool active() const;
  bool error() const;
  std::uint64_t displayDrops() const;
  CaptureMetrics metrics(bool includeEvents=true) const;
  std::string setProfile(std::vector<Signal>);
  void setTraceSignals(std::vector<unsigned>);
  std::shared_ptr<const LiveFrame> liveFrame() const;
  std::shared_ptr<const RtMonitorFrame> rtFrame() const;
};
}
