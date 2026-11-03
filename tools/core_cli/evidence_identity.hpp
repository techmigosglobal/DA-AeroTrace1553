#pragma once
#include "ddc_runtime.hpp"
#include "capture_time.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

namespace bench {
inline std::string pathText(const std::filesystem::path &path) {
  const auto value=path.u8string();return {value.begin(),value.end()};
}
inline std::string executableSha256() {
#ifdef _WIN32
  wchar_t path[32768]{};const auto length=GetModuleFileNameW(nullptr,path,32768);
  if(length&&length<32768)return aero::ddcRuntimeSha256(std::filesystem::path(path));
#endif
  return {};
}
}
