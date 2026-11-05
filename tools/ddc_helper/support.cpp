#include "helper_windows.hpp"
namespace aero {std::string ddcRuntimeSha256(const std::filesystem::path &path){return helper::hashFile(path);}}
