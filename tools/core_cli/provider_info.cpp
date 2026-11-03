#include "source.hpp"
#include "ddc_runtime.hpp"
#include <iostream>
int main(int argc,char **argv){if(argc!=2){std::cerr<<"Usage: aerotrace_provider_info <SDK-folder-or-runtime-DLL> (never opens a card)\n";return 2;}const auto info=aero::inspectDdcRuntime(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argv[1]))));std::cout<<(info.available?"PASS: ":"BLOCKED: ")<<info.message<<'\n';return info.available?0:2;}
