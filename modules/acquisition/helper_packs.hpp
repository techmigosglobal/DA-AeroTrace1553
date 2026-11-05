#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace aero {
struct DdcHelperPack {
  std::filesystem::path manifest,helperPath,runtimePath;
  std::string id,sdkIdentity,helperSha256,runtimeSha256,headerSha256,error;
  bool softwareVerified{},automaticApproved{},fixture{};
  std::vector<std::string> modes;
};
DdcHelperPack readDdcHelperPack(const std::filesystem::path &);
std::vector<DdcHelperPack> discoverDdcHelperPacks();
}
