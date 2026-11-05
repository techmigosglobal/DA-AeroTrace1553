#include "source.hpp"
#include "ddc_runtime.hpp"
#include "capture_time.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
namespace {
std::string utf8(const std::filesystem::path &p){const auto v=p.u8string();return {v.begin(),v.end()};}
}
int main(int argc,char **argv){
#ifdef _WIN32
  int wideCount{};auto wideArgs=CommandLineToArgvW(GetCommandLineW(),&wideCount);
  struct ReleaseArguments {LPWSTR *value;~ReleaseArguments(){if(value)LocalFree(value);}} releaseArguments{wideArgs};
#endif
  bool json=false,inventory=false;std::filesystem::path selected;
  for(int i=1;i<argc;++i){const std::string arg=argv[i];
    if(arg=="--json")json=true;
    else if(arg=="--inventory")inventory=true;
    else if(arg=="--help"){std::cout<<"aerotrace_provider_info [--json] [--inventory | <SDK-folder-or-runtime-DLL>]\nInspects runtime/provider identities; never opens a card. No driver or registry changes.\n";return 0;}
    else if(arg.starts_with("--")||!selected.empty()){std::cerr<<"Unknown option or multiple runtime paths\n";return 2;}
    else {
#ifdef _WIN32
      if(wideArgs&&i<wideCount)selected=std::filesystem::path(wideArgs[i]);
      else
#endif
      selected=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argv[i])));
    }
  }
  if(selected.empty()&&!inventory){std::cerr<<"Specify --inventory or an SDK folder/runtime DLL; use --help.\n";return 2;}
  if(inventory&&!selected.empty()){std::cerr<<"--inventory cannot be combined with an explicit runtime path\n";return 2;}
  const auto selection=json?aero::selectDdcRuntime(selected):aero::DdcRuntimeSelection{};
  const auto info=aero::inspectDdcRuntime(selected);
  if(json){
    using aero::jsonString;
    std::cout<<"{\"schema\":\"aerotrace.ddc-runtime-report.v2\",\"runtime_detected\":"<<(info.runtimeDetected?"true":"false")
      <<",\"runtime_loadable\":"<<(info.runtimeLoadable?"true":"false")<<",\"provider_available\":"<<(info.providerAvailable?"true":"false")
      <<",\"automatic_approved\":"<<(info.automaticApproved?"true":"false")<<",\"runtime_path\":"<<jsonString(utf8(info.path))
      <<",\"file_version\":"<<jsonString(info.version)<<",\"architecture\":"<<jsonString(info.architecture)<<",\"sha256\":"<<jsonString(info.sha256)<<",\"provider_id\":"<<jsonString(info.providerId)
      <<",\"provider_path\":"<<jsonString(utf8(info.providerPath))<<",\"stage\":"<<jsonString(info.stage)<<",\"qualification\":"<<jsonString(info.qualification)
      <<",\"message\":"<<jsonString(info.message)<<",\"card_opened\":false,\"runtime_selection_error\":"<<jsonString(selection.error)<<",\"runtime_candidates\":[";
    bool first=true;
    for(const auto &c:selection.candidates){if(!first)std::cout<<',';first=false;
      std::cout<<"{\"path\":"<<jsonString(utf8(c.path))<<",\"origin\":"<<jsonString(c.origin)<<",\"file_version\":"<<jsonString(c.version)
        <<",\"architecture\":"<<jsonString(c.architecture)<<",\"sha256\":"<<jsonString(c.sha256)<<",\"error\":"<<jsonString(c.error)<<",\"stage\":"<<jsonString(c.stage)
        <<",\"detected\":"<<(c.detected?"true":"false")<<",\"loadable\":"<<(c.loadable?"true":"false")<<",\"usable\":"<<(c.usable?"true":"false")
        <<",\"provider_available\":"<<(c.providerAvailable?"true":"false")<<",\"provider_id\":"<<jsonString(c.providerId)
        <<",\"provider_path\":"<<jsonString(utf8(c.providerPath))<<",\"automatic_approved\":"<<(c.automaticApproved?"true":"false")
        <<",\"qualification\":"<<jsonString(c.qualification)<<",\"driver_version_match\":"<<(c.driverMatch?"true":"false")
        <<",\"mt_receive_interface\":"<<(c.legacyMt?"true":"false")<<",\"mti_receive_interface\":"<<(c.mti?"true":"false")<<'}';
    }
    std::cout<<"]}\n";
  }else std::cout<<(info.available?"PASS: ":"BLOCKED: ")<<info.message<<"\nRuntime: "<<utf8(info.path)<<"\nVersion: "<<info.version<<"\nSHA-256: "<<info.sha256<<'\n';
  return info.available?0:2;
}
