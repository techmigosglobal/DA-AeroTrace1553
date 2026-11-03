#pragma once
#include "core.hpp"
#include "signal_checks.hpp"
#include <array>
#include <memory>
#include <stop_token>
#include <functional>

namespace aero {
enum class DevicePhase { Disabled, Checking, Ready, Monitoring, Simulating, Stopped, Failed };
struct DeviceState { DevicePhase phase{DevicePhase::Stopped}; std::string message; };
struct CaptureConfig {
  bool hardware{true};
  std::string runId;
  std::array<bool,4> enabled{true,true,false,false};
  std::array<int,2> logicalDevices{0,1};
  std::filesystem::path runtime;
  unsigned simulatorRate{12};
  // Empty profile preserves the original demo generator.
  std::vector<Signal> simulatorProfile;
  std::string simulatorWave{"sine"};
  double simulatorAmplitude{10},simulatorOffset{},simulatorFrequency{1};
  std::uint64_t simulatorSeed{1};
  unsigned simulatorDropEvery{},simulatorFlagEvery{};
  std::vector<CheckRule> checkRules;
  std::size_t displayCapacity{8192};
  std::function<void(unsigned,DevicePhase,const std::string &)> deviceState;
  std::function<void(const std::string &,int,const std::string &)> diagnostic;
  std::stop_token stopToken;
};
struct SourceBatch {
  std::vector<Record> records;
  struct Packet { unsigned channel{}; std::vector<std::uint8_t> bytes;bool chapter10{true}; };
  std::vector<Packet> packets;
  std::uint64_t hardwareLost{}, hardwareOverflows{};
  // Observed per-channel high-water counters, not a claimed lifetime loss total.
  std::array<std::uint64_t,2> channelLost{},channelOverflows{};
};
class CaptureSource {
public:
  virtual ~CaptureSource()=default;
  virtual std::string open(const CaptureConfig &)=0;
  virtual std::string startMonitoring() {return {};}
  virtual void updateCallbacks(const CaptureConfig &) {}
  virtual std::string poll(SourceBatch &,std::stop_token)=0;
  virtual std::string finish(SourceBatch &) {return {};}
  virtual void close()=0;
  virtual std::string closeError() const {return {};}
};
std::unique_ptr<CaptureSource> makeSimulator();
std::string validateSimulation(const CaptureConfig &);
Record simulationRecord(const CaptureConfig &,std::uint64_t index);
std::unique_ptr<CaptureSource> makeDdcSource();
struct DdcRuntimeInfo { bool available{};std::string message;std::filesystem::path path; };
DdcRuntimeInfo inspectDdcRuntime(const std::filesystem::path &path={});
}
