#include "ddc_runtime.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdint>
#include <cwctype>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif
namespace aero {
#ifdef _WIN32
namespace {
std::string utf8(const std::filesystem::path &path){const auto s=path.u8string();return {s.begin(),s.end()};}
std::array<unsigned,3> versionParts(const std::string &s){std::array<unsigned,3> result{};char dot;std::istringstream stream(s);stream>>result[0]>>dot>>result[1]>>dot>>result[2];return result;}
std::string fileVersion(const std::filesystem::path &path){
  DWORD unused{};const auto size=GetFileVersionInfoSizeW(path.c_str(),&unused);std::vector<unsigned char> data(size);
  if(!size||!GetFileVersionInfoW(path.c_str(),0,size,data.data()))return {};
  VS_FIXEDFILEINFO *v{};UINT bytes{};
  if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void **>(&v),&bytes)||!v||bytes<sizeof(*v))return {};
  return std::to_string(HIWORD(v->dwFileVersionMS))+"."+std::to_string(LOWORD(v->dwFileVersionMS))+"."+std::to_string(HIWORD(v->dwFileVersionLS))+"."+std::to_string(LOWORD(v->dwFileVersionLS));
}
std::set<std::array<unsigned,3>> driverVersions(){
  std::set<std::array<unsigned,3>> result;
  const auto set=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);if(set==INVALID_HANDLE_VALUE)return result;
  SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
  for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&device);++i){
    wchar_t ids[8192]{};DWORD type{};
    SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_HARDWAREID,&type,reinterpret_cast<PBYTE>(ids),sizeof(ids),nullptr);
    std::wstring hardware(ids);std::transform(hardware.begin(),hardware.end(),hardware.begin(),::towupper);
    wchar_t service[128]{};
    SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_SERVICE,&type,reinterpret_cast<PBYTE>(service),sizeof(service),nullptr);
    const std::wstring svc(service);
    if(hardware.find(L"VID_4DDC")==std::wstring::npos&&svc!=L"acexpci"&&svc!=L"acexusb"&&svc!=L"e2mausb"&&svc!=L"emapci"&&svc!=L"emaebrpci")continue;
    ULONG status{},problem{};if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)!=CR_SUCCESS||problem||!(status&DN_STARTED))continue;
    const auto key=SetupDiOpenDevRegKey(set,&device,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);
    if(key==INVALID_HANDLE_VALUE)continue;
    wchar_t version[256]{};DWORD bytes=sizeof(version);
    if(RegQueryValueExW(key,L"DriverVersion",nullptr,&type,reinterpret_cast<LPBYTE>(version),&bytes)==ERROR_SUCCESS){const auto v=utf8(version);result.insert(versionParts(v));}
    RegCloseKey(key);
  }
  SetupDiDestroyDeviceInfoList(set);return result;
}
DdcRuntimeCandidate inspect(std::filesystem::path path,const std::string &origin,const std::set<std::array<unsigned,3>> &drivers){
  DdcRuntimeCandidate c;c.path=std::filesystem::absolute(path);c.origin=origin;c.version=fileVersion(c.path);
  // Fail explicitly on a wrong-architecture image before executing its loader entry point.
  std::ifstream file(c.path,std::ios::binary);std::uint16_t mz{},machine{};std::uint32_t offset{},signature{};
  file.read(reinterpret_cast<char *>(&mz),2);file.seekg(0x3c);file.read(reinterpret_cast<char *>(&offset),4);
  if(offset>16*1024*1024){c.error="Invalid PE header";return c;}
  file.seekg(offset);file.read(reinterpret_cast<char *>(&signature),4);file.read(reinterpret_cast<char *>(&machine),2);
  if(!file||mz!=0x5a4d||signature!=0x4550||machine!=0x8664){c.error="DDC runtime must be a valid x64 PE DLL";return c;}
  HMODULE module=LoadLibraryExW(c.path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module){c.error="Windows loader error "+std::to_string(GetLastError());return c;}
  for(const auto *name:{"aceGetLibVersion","aceErrorStr","aceInitialize","aceGetHwVersionInfo","aceFree","aceSetTimeTagRes"})
    if(!GetProcAddress(module,name)){c.error=std::string("Missing DDC export: ")+name;break;}
  if(c.error.empty()){
    using Version=unsigned short (__stdcall *)();c.api=reinterpret_cast<Version>(GetProcAddress(module,"aceGetLibVersion"))();
    for(const auto *name:{"aceMTConfigure","aceMTInstallHBuf","aceMTGetHBufMsgsRaw","aceMTDecodeRawMsg","aceMTStart","aceMTStop"})
      if(!GetProcAddress(module,name))c.missingMt.emplace_back(name);
    for(const auto *name:{"aceMTIConfigure","aceMTIGetCh10DataPkt","aceMTIDecodeRawMsg","aceMTIStart","aceMTIStop","aceMTIFreeCh10DataPkt","aceMtiGetMetrics"})
      if(!GetProcAddress(module,name))c.missingMti.emplace_back(name);
    c.legacyMt=c.missingMt.empty();c.mti=c.missingMti.empty();
    if(!c.legacyMt&&!c.mti)c.error="DDC runtime exposes neither complete MT nor MTI receive interface (including packet release and loss metrics)";
  }
  FreeLibrary(module);c.usable=c.error.empty();c.driverMatch=!c.version.empty()&&drivers.contains(versionParts(c.version));return c;
}
}
#endif
DdcRuntimeSelection selectDdcRuntime(const std::filesystem::path &overridePath){
  DdcRuntimeSelection result;
#ifdef _WIN32
  const auto drivers=driverVersions();std::set<std::wstring> seen;
  auto add=[&](const std::filesystem::path &path,const std::string &origin){
    std::error_code ec;if(!std::filesystem::is_regular_file(path,ec))return;
    auto normalized=std::filesystem::weakly_canonical(path,ec).wstring();std::transform(normalized.begin(),normalized.end(),normalized.begin(),::towlower);
    if(!seen.insert(normalized).second)return;
    result.candidates.push_back(inspect(path,origin,drivers));
  };
  auto sdk=[&](const std::filesystem::path &root,const std::string &origin){
    add(root/"Lib/x64/Release/emacepl.dll",origin);
    add(root/"libraries/emacepl/bin/x64/Release/emacepl.dll",origin);
    add(root/"emacepl.dll",origin);
  };
  auto choose=[&](const auto &values){
    if(values.empty())return false;
    const auto &first=*values.front();
    for(const auto *c:values)if(c->version!=first.version||c->api!=first.api||c->legacyMt!=first.legacyMt||c->mti!=first.mti)return false;
    result.path=first.path;return true;
  };
  auto configuredSdk=[&](const std::filesystem::path &root,const std::string &origin){
    sdk(root,origin);std::vector<const DdcRuntimeCandidate *> usable;
    for(const auto &c:result.candidates)if(c.usable)usable.push_back(&c);
    if(result.candidates.empty())result.error="Configured DDC SDK folder contains no x64 emacepl.dll in a supported layout: "+utf8(root);
    else if(usable.empty()){
      result.error="Configured DDC SDK has no usable receive runtime: "+utf8(root);
      for(const auto &c:result.candidates)result.error+="; "+utf8(c.path)+": "+c.error;
    }else if(!choose(usable))result.error="Configured DDC SDK folder contains conflicting receive runtimes. Select the exact emacepl.dll in Device Setup: "+utf8(root);
  };
  if(!overridePath.empty()){
    std::error_code ec;
    if(std::filesystem::is_directory(overridePath,ec)){
      configuredSdk(overridePath,"manual SDK folder");return result;
    }
    add(overridePath,"manual override");
    if(result.candidates.empty())result.error="Selected DDC runtime does not exist: "+utf8(overridePath);
    else if(!result.candidates[0].usable)result.error="Selected DDC runtime is incompatible: "+result.candidates[0].error;
    else result.path=result.candidates[0].path;
    return result;
  }
  // Honour the machine's explicit SDK configuration before packaged/build copies.
  // An invalid configuration must not silently select a different SDK.
  wchar_t env[32768]{};const auto rootLength=GetEnvironmentVariableW(L"DDC_SDK_ROOT",env,32768);
  if(rootLength){
    if(rootLength>=32768)result.error="DDC_SDK_ROOT exceeds the supported Windows path length";
    else configuredSdk(env,"DDC_SDK_ROOT");
    return result;
  }
  std::vector<std::filesystem::path> roots{L"C:/DDC"};
  for(const auto *name:{L"ProgramFiles",L"ProgramFiles(x86)"})if(GetEnvironmentVariableW(name,env,32768))roots.push_back(std::filesystem::path(env)/L"DDC");
  // Installer-owned roots are read from both registry views; arbitrary PATH DLLs are never loaded.
  for(const REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER}){
    HKEY uninstall{};if(RegOpenKeyExW(hive,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&uninstall)!=ERROR_SUCCESS)continue;
    wchar_t sub[512]{};DWORD length=512;
    for(DWORD i=0;RegEnumKeyExW(uninstall,i,sub,&length,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS;++i,length=512){
      HKEY item{};if(RegOpenKeyExW(uninstall,sub,0,KEY_READ,&item)!=ERROR_SUCCESS)continue;
      wchar_t display[1024]{},location[32768]{};DWORD bytes=sizeof(display),type{};
      RegQueryValueExW(item,L"DisplayName",nullptr,&type,reinterpret_cast<LPBYTE>(display),&bytes);
      std::wstring title(display);std::transform(title.begin(),title.end(),title.begin(),::towlower);
      if(title.find(L"acextreme")!=std::wstring::npos){bytes=sizeof(location);if(RegQueryValueExW(item,L"InstallLocation",nullptr,&type,reinterpret_cast<LPBYTE>(location),&bytes)==ERROR_SUCCESS&&location[0])roots.emplace_back(location);}
      RegCloseKey(item);
    }
    RegCloseKey(uninstall);
  }
  for(const auto &root:roots){
    sdk(root,"installed SDK");std::error_code ec;
    for(std::filesystem::directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end&&!ec;it.increment(ec))if(it->is_directory(ec))sdk(it->path(),"installed SDK");
  }
  wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
  add(std::filesystem::path(executable).parent_path()/"emacepl.dll","packaged runtime");
#ifdef AEROTRACE_DDC_RUNTIME
  add(std::filesystem::path(AEROTRACE_DDC_RUNTIME),"build SDK");
#endif
  std::vector<const DdcRuntimeCandidate *> matches,installed,fallback;
  for(const auto &c:result.candidates)if(c.usable){if(c.driverMatch)matches.push_back(&c);if(c.origin=="installed SDK")installed.push_back(&c);else fallback.push_back(&c);}
  // Prefer installed SDK evidence; reject conflicting versions or receive interfaces.
  if(!choose(matches)&&!choose(installed)){
    if(!installed.empty()||!matches.empty())result.error="Multiple DDC runtime versions are installed and cannot be selected unambiguously. Select a runtime in Device Setup.";
    else if(!choose(fallback))result.error="No compatible x64 DDC runtime found. Install the matching aceXtreme SDK/driver or select emacepl.dll in Device Setup.";
  }
#else
  result.error="DDC runtime discovery requires Windows";
#endif
  return result;
}
}
