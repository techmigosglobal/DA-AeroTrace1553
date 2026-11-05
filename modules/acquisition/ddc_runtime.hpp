#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "provider_pack.hpp"
namespace aero {
struct DdcRuntimeCandidate {
  std::filesystem::path path;
  std::string origin,version,error;
  unsigned api{};
  bool usable{},driverMatch{};
  bool legacyMt{},mti{}; // Complete receive interfaces, not physical qualification.
  std::vector<std::string> missingMt,missingMti;
  std::string sha256,stage,providerId,qualification;
  std::filesystem::path providerPath,packManifest;
  bool detected{},loadable{},providerAvailable{},automaticApproved{},probeBlocked{};
  std::string architecture{"unknown"};
};
struct DdcRuntimeSelection {
  std::filesystem::path path;
  std::string error;
  std::vector<DdcRuntimeCandidate> candidates;
  bool explicitSelection{},probeBlocked{};
};
// Discovery loads candidate DLLs only to inspect exports/version; never opens a card.
DdcRuntimeSelection selectDdcRuntime(const std::filesystem::path &overridePath={},bool probe=true);
std::string ddcRuntimeSha256(const std::filesystem::path &);
// No lock crosses lifecycle threads. Probes atomically promote to connection ownership.
class DdcRuntimeProbe {
  bool admitted_{};
public:
  explicit DdcRuntimeProbe(bool request=true);
  ~DdcRuntimeProbe();
  DdcRuntimeProbe(const DdcRuntimeProbe &)=delete;
  DdcRuntimeProbe &operator=(const DdcRuntimeProbe &)=delete;
  bool admitted() const{return admitted_;}
  bool promoteToConnection();
};
bool ddcProviderConnectionActive();
void releaseDdcProviderConnection();
struct DdcDriverIdentity {std::string version;std::vector<std::string> hardwareIds;};
bool ddcPackAutomaticApproved(const DdcProviderPack &,unsigned windowsBuild,const std::vector<DdcDriverIdentity> &);
std::vector<DdcProviderPack> discoverDdcProviderPacks();
}
