#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace aero {
struct DdcRuntimeCandidate {
  std::filesystem::path path;
  std::string origin,version,error;
  unsigned api{};
  bool usable{},driverMatch{};
  bool legacyMt{},mti{}; // Complete receive interfaces, not physical qualification.
  std::vector<std::string> missingMt,missingMti;
};
struct DdcRuntimeSelection {
  std::filesystem::path path;
  std::string error;
  std::vector<DdcRuntimeCandidate> candidates;
};
// Discovery loads candidate DLLs only to inspect exports/version; never opens a card.
DdcRuntimeSelection selectDdcRuntime(const std::filesystem::path &overridePath={});
std::string ddcRuntimeSha256(const std::filesystem::path &);
}
