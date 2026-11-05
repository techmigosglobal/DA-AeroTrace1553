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
enum class DdcBackend { NativeX64, HelperX86 };
enum class DdcReceiveMode { Auto, MTI, LegacyMT };
enum class DdcConnectionPolicy { QualifiedAutomatic, Compatibility };
enum class CaptureFailureCategory { None, RuntimeSelection, Assignment, Registry, DriverOpen, Access, Ownership, UnsupportedMode, Configuration, Integrity, Transport, Storage, Cleanup, Unknown };
struct CaptureFailure {
  CaptureFailureCategory category{CaptureFailureCategory::None};
  std::string stage,operation,detail;
  int code{},channel{-1},logicalDevice{-1}; // Channel is zero based; -1 denotes a global failure.
  std::uint32_t windowsCode{};
  bool supplementaryWindows{};
  bool cleanupFailed{};
  std::vector<std::string> recovery;
  explicit operator bool() const { return category!=CaptureFailureCategory::None; }
};
struct CaptureIdentity {
  std::string backend,receiveMode,providerId,sdkIdentity,headerSha256,runtimeSha256,helperSha256,qualification;
  std::filesystem::path runtime,provider,helper,manifest;
};
struct CaptureConfig {
  bool hardware{true};
  std::string runId;
  std::array<bool,4> enabled{true,true,false,false};
  std::array<int,2> logicalDevices{0,1};
  std::filesystem::path runtime;
  DdcBackend backend{DdcBackend::NativeX64};
  DdcReceiveMode receiveMode{DdcReceiveMode::Auto};
  DdcConnectionPolicy connectionPolicy{DdcConnectionPolicy::QualifiedAutomatic};
  std::filesystem::path helperPack;
  // Set by the verified native provider or helper after pack identity validation.
  std::string runtimeSha256,runtimeArchitecture{"x64"};
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
  std::function<bool()> cancelRequested; // Provider-local bridge; never passed across the DLL ABI.
};
struct SourceBatch {
  std::vector<Record> records;
  struct Packet { unsigned channel{}; std::vector<std::uint8_t> bytes;bool chapter10{true}; };
  std::vector<Packet> packets;
  std::uint64_t hardwareLost{}, hardwareOverflows{};
  std::uint64_t pollNs{},packetProcessingNs{};
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
  virtual std::string stopMonitoring() {return {};}
  virtual std::string drainNext(SourceBatch &out,bool &more) {more=false;return finish(out);}
  virtual void close()=0;
  virtual std::string closeError() const {return {};}
  virtual CaptureFailure lastFailure() const {return {};}
  virtual CaptureIdentity identity() const {return {};}
};
std::unique_ptr<CaptureSource> makeSimulator();
std::string validateSimulation(const CaptureConfig &);
Record simulationRecord(const CaptureConfig &,std::uint64_t index);
std::unique_ptr<CaptureSource> makeDdcSource();
std::unique_ptr<CaptureSource> makeDdcHelperSource();
std::unique_ptr<CaptureSource> makeSdkOwnedSource(std::unique_ptr<CaptureSource>);
struct DdcRuntimeInfo {
  bool available{};std::string message;std::filesystem::path path;
  bool runtimeDetected{},runtimeLoadable{},providerAvailable{},probeBlocked{},automaticApproved{};
  std::string version,sha256,providerId,stage,qualification;
  std::filesystem::path providerPath;
  std::string architecture{"unknown"};
};
DdcRuntimeInfo inspectDdcRuntime(const std::filesystem::path &path={},bool probe=true);
bool ddcProviderConnectionActive();
}
