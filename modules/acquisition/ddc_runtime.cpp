#include "ddc_runtime.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdint>
#include <cwctype>
#include <set>
#include <sstream>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#endif
namespace aero {
std::string ddcRuntimeSha256(const std::filesystem::path &path){
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
  struct Cleanup {BCRYPT_ALG_HANDLE &a;BCRYPT_HASH_HANDLE &h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
  if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)return {};
  std::ifstream input(path,std::ios::binary);if(!input)return {};std::array<unsigned char,65536> buffer{};
  while(input){input.read(reinterpret_cast<char *>(buffer.data()),buffer.size());if(input.gcount()&&BCryptHashData(hash,buffer.data(),ULONG(input.gcount()),0)<0)return {};}
  if(!input.eof())return {};std::array<unsigned char,32> digest{};if(BCryptFinishHash(hash,digest.data(),digest.size(),0)<0)return {};
  constexpr char hex[]="0123456789abcdef";std::string text;for(auto b:digest){text+=hex[b>>4];text+=hex[b&15];}return text;
#else
  (void)path;return {};
#endif
}

namespace {
std::mutex admissionMutex;unsigned activeProbes{};bool activeConnection{};
std::string lower(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(c>='A'&&c<='Z'?c-'A'+'a':c);});return value;}
std::string pathText(const std::filesystem::path &p){const auto s=p.u8string();return {s.begin(),s.end()};}
}
DdcRuntimeProbe::DdcRuntimeProbe(bool request){std::lock_guard lock(admissionMutex);if(request&&!activeConnection){++activeProbes;admitted_=true;}}
DdcRuntimeProbe::~DdcRuntimeProbe(){std::lock_guard lock(admissionMutex);if(admitted_)--activeProbes;}
bool DdcRuntimeProbe::promoteToConnection(){std::lock_guard lock(admissionMutex);if(!admitted_||activeConnection||activeProbes!=1)return false;--activeProbes;admitted_=false;activeConnection=true;return true;}
bool ddcProviderConnectionActive(){std::lock_guard lock(admissionMutex);return activeConnection;}
void releaseDdcProviderConnection(){std::lock_guard lock(admissionMutex);activeConnection=false;}
bool ddcPackAutomaticApproved(const DdcProviderPack &pack,unsigned build,const std::vector<DdcDriverIdentity> &drivers){
  if(!pack.error.empty()||!pack.receiptsComplete||!build||drivers.empty())return false;
  // Every active DDC binding must be covered by a finite exact driver/HWID row.
  for(const auto &driver:drivers){bool approved=false;for(const auto &row:pack.matrix){
    if(row.receipt.empty()||!row.windowsMinBuild||row.windowsMaxBuild<row.windowsMinBuild||row.hardwareId.find('*')!=std::string::npos||row.hardwareId.find('?')!=std::string::npos||build<row.windowsMinBuild||build>row.windowsMaxBuild||driver.version!=row.driverVersion)continue;
    for(const auto &id:driver.hardwareIds)if(lower(id)==lower(row.hardwareId))approved=true;
  }if(!approved)return false;}return true;
}
std::vector<DdcProviderPack> discoverDdcProviderPacks(){
  std::vector<DdcProviderPack> result;
#ifdef _WIN32
  std::vector<std::filesystem::path> roots;wchar_t env[32768]{};
  const auto configured=GetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER_PACKS",env,32768);
  if(configured&&configured<32768){const std::filesystem::path requested(env);if(requested.is_absolute())roots.push_back(requested);}
  else if(!configured){wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);roots.push_back(std::filesystem::path(executable).parent_path()/"providers/packs");
    for(const auto *name:{L"LOCALAPPDATA",L"ProgramData"})if(GetEnvironmentVariableW(name,env,32768))roots.push_back(std::filesystem::path(env)/"AeroTrace1553/provider-packs");}
  std::set<std::filesystem::path> seen;
  for(const auto &root:roots){std::error_code ec;
    auto append=[&](const auto &manifest){if(result.size()>=128)return;auto canonical=std::filesystem::weakly_canonical(manifest,ec);if(ec||!seen.insert(canonical).second)return;result.push_back(readDdcProviderPack(canonical));};
    if(std::filesystem::is_regular_file(root/"provider-pack.json",ec))append(root/"provider-pack.json");
    unsigned examined=0;for(std::filesystem::directory_iterator i(root,std::filesystem::directory_options::skip_permission_denied,ec),end;i!=end&&!ec&&examined<256;i.increment(ec),++examined)
      if(!i->is_symlink(ec)&&i->is_directory(ec)&&std::filesystem::is_regular_file(i->path()/"provider-pack.json",ec))append(i->path()/"provider-pack.json");
  }
#endif
  return result;
}
#ifdef _WIN32
namespace {
std::string utf8(const std::filesystem::path &path){return pathText(path);}
std::array<unsigned,3> versionParts(const std::string &s){std::array<unsigned,3> result{};char dot;std::istringstream stream(s);stream>>result[0]>>dot>>result[1]>>dot>>result[2];return result;}
std::string fileVersion(const std::filesystem::path &path){
  DWORD unused{};const auto size=GetFileVersionInfoSizeW(path.c_str(),&unused);std::vector<unsigned char> data(size);
  if(!size||!GetFileVersionInfoW(path.c_str(),0,size,data.data()))return {};
  VS_FIXEDFILEINFO *v{};UINT bytes{};if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void **>(&v),&bytes)||!v||bytes<sizeof(*v))return {};
  return std::to_string(HIWORD(v->dwFileVersionMS))+"."+std::to_string(LOWORD(v->dwFileVersionMS))+"."+std::to_string(HIWORD(v->dwFileVersionLS))+"."+std::to_string(LOWORD(v->dwFileVersionLS));
}
unsigned windowsBuild(){using Version=LONG(WINAPI *)(OSVERSIONINFOW *);auto module=GetModuleHandleW(L"ntdll.dll");auto get=module?reinterpret_cast<Version>(GetProcAddress(module,"RtlGetVersion")):nullptr;OSVERSIONINFOW value{};value.dwOSVersionInfoSize=sizeof(value);return get&&get(&value)==0?value.dwBuildNumber:0;}
std::vector<DdcDriverIdentity> driverIdentities(){
  std::vector<DdcDriverIdentity> result;const auto set=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);if(set==INVALID_HANDLE_VALUE)return result;
  SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
  for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&device);++i){
    wchar_t ids[8192]{};DWORD type{},required{};const bool haveIds=SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_HARDWAREID,&type,reinterpret_cast<PBYTE>(ids),sizeof(ids),&required)!=FALSE;
    wchar_t service[128]{};SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_SERVICE,&type,reinterpret_cast<PBYTE>(service),sizeof(service),nullptr);
    const auto svc=lower(utf8(service));DdcDriverIdentity identity;
    if(haveIds&&required<=sizeof(ids))for(std::size_t start=0;start<8191&&ids[start];){std::size_t end=start;while(end<8192&&ids[end])++end;if(end==8192)break;identity.hardwareIds.push_back(utf8(std::wstring(ids+start,ids+end)));start=end+1;}
    bool known=svc=="acexpci"||svc=="acexusb"||svc=="e2mausb"||svc=="emapci"||svc=="emaebrpci";
    for(const auto &id:identity.hardwareIds){auto value=lower(id);if(value.find("vid_4ddc")!=std::string::npos||value.find("ven_4ddc")!=std::string::npos||value.find("ven_ddc")!=std::string::npos)known=true;}
    if(!known)continue;ULONG status{},problem{};if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)!=CR_SUCCESS||problem||!(status&DN_STARTED))continue;
    const auto key=SetupDiOpenDevRegKey(set,&device,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);if(key==INVALID_HANDLE_VALUE)continue;
    wchar_t version[256]{};DWORD bytes=sizeof(version);if(RegQueryValueExW(key,L"DriverVersion",nullptr,&type,reinterpret_cast<LPBYTE>(version),&bytes)==ERROR_SUCCESS)identity.version=utf8(version);RegCloseKey(key);
    result.push_back(std::move(identity));
  }SetupDiDestroyDeviceInfoList(set);return result;
}
std::string peArchitecture(const std::filesystem::path &path){
  std::error_code sizeError;const auto bytes=std::filesystem::file_size(path,sizeError);if(sizeError||bytes>256*1024*1024)return "unknown";
  std::ifstream file(path,std::ios::binary);std::uint16_t mz{},machine{};std::uint32_t offset{},signature{};
  file.read(reinterpret_cast<char *>(&mz),2);file.seekg(0x3c);file.read(reinterpret_cast<char *>(&offset),4);if(!file||offset>16*1024*1024)return "unknown";
  file.seekg(offset);file.read(reinterpret_cast<char *>(&signature),4);file.read(reinterpret_cast<char *>(&machine),2);if(!file||mz!=0x5a4d||signature!=0x4550)return "unknown";
  switch(machine){case 0x14c:return "x86";case 0x8664:return "x64";case 0xaa64:return "arm64";default:return "unknown";}
}
bool x64Image(const std::filesystem::path &path){return peArchitecture(path)=="x64";}
DdcRuntimeCandidate inspect(std::filesystem::path path,const std::string &origin,const std::vector<DdcDriverIdentity> &drivers,bool probe){
  DdcRuntimeCandidate c;c.path=std::filesystem::absolute(path);c.origin=origin;c.detected=true;c.version=fileVersion(c.path);std::error_code sizeError;const auto bytes=std::filesystem::file_size(c.path,sizeError);if(!sizeError&&bytes<=256*1024*1024)c.sha256=ddcRuntimeSha256(c.path);c.stage="runtime_detected";
  c.architecture=peArchitecture(c.path);
  if(c.architecture!="x64"){c.error="DDC runtime must be a valid x64 PE DLL";c.stage="runtime_invalid";return c;}
  for(const auto &driver:drivers)if(!c.version.empty()&&versionParts(driver.version)==versionParts(c.version))c.driverMatch=true;
  if(!probe){c.probeBlocked=ddcProviderConnectionActive();c.stage=c.probeBlocked?"probe_blocked":"runtime_identified";return c;}
  HMODULE module=LoadLibraryExW(c.path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module){c.error="Windows loader error "+std::to_string(GetLastError());c.stage="runtime_unloadable";return c;}
  c.loadable=true;
  for(const auto *name:{"aceGetLibVersion","aceErrorStr","aceInitialize","aceGetHwVersionInfo","aceFree","aceSetTimeTagRes"})if(!GetProcAddress(module,name)){c.error=std::string("Missing DDC export: ")+name;break;}
  if(c.error.empty()){
    using Version=unsigned short(__stdcall *)();c.api=reinterpret_cast<Version>(GetProcAddress(module,"aceGetLibVersion"))();
    for(const auto *name:{"aceMTConfigure","aceMTInstallHBuf","aceMTGetHBufMsgsRaw","aceMTDecodeRawMsg","aceMTStart","aceMTStop"})if(!GetProcAddress(module,name))c.missingMt.emplace_back(name);
    for(const auto *name:{"aceMTIConfigure","aceMTIGetCh10DataPkt","aceMTIDecodeRawMsg","aceMTIStart","aceMTIStop","aceMTIFreeCh10DataPkt","aceMtiGetMetrics"})if(!GetProcAddress(module,name))c.missingMti.emplace_back(name);
    c.legacyMt=c.missingMt.empty();c.mti=c.missingMti.empty();if(!c.legacyMt&&!c.mti)c.error="DDC runtime exposes neither complete MT nor MTI receive interface (including packet release and loss metrics)";
  }FreeLibrary(module);c.usable=c.error.empty();c.stage=c.usable?"runtime_ready":"runtime_incompatible";return c;
}
}
#endif
DdcRuntimeSelection selectDdcRuntime(const std::filesystem::path &overridePath,bool probe){
  DdcRuntimeSelection result;
#ifdef _WIN32
  DdcRuntimeProbe admission(probe);const bool mayProbe=probe&&admission.admitted();result.probeBlocked=probe&&!mayProbe;
  const auto drivers=driverIdentities();const auto packs=discoverDdcProviderPacks();const auto build=windowsBuild();std::set<std::wstring> seen;
  wchar_t providerOverrideBuffer[32768]{};const bool providerOverride=GetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",providerOverrideBuffer,32768)>0;
  auto add=[&](const std::filesystem::path &path,const std::string &origin){
    std::error_code ec;if(!std::filesystem::is_regular_file(path,ec))return;auto normalized=std::filesystem::weakly_canonical(path,ec).wstring();if(ec)return;std::transform(normalized.begin(),normalized.end(),normalized.begin(),::towlower);if(!seen.insert(normalized).second)return;
    auto c=inspect(path,origin,drivers,mayProbe);
    for(const auto &pack:packs){if(providerOverride||pack.runtimePath.empty()||pack.runtimeSha256!=c.sha256)continue;
      if(!pack.error.empty()){c.error=pack.error;c.stage="pack_invalid";continue;}
      if(!x64Image(pack.providerPath)||ddcRuntimeSha256(pack.providerPath)!=pack.providerSha256){c.error="Provider pack DLL architecture or SHA-256 mismatch";c.stage="pack_invalid";continue;}
      if(!c.providerPath.empty()&&c.providerPath!=pack.providerPath){
        const auto previous=readDdcProviderPack(c.packManifest);
        if(c.providerId!=pack.id||previous.providerSha256!=pack.providerSha256||previous.sdkIdentity!=pack.sdkIdentity||previous.headerSha256!=pack.headerSha256){c.error="Ambiguous provider packs for runtime fingerprint";c.stage="provider_ambiguous";c.providerAvailable=false;break;}
        if(c.automaticApproved||!ddcPackAutomaticApproved(pack,build,drivers))continue;
      }
      c.providerPath=pack.providerPath;c.packManifest=pack.manifest;c.providerId=pack.id;c.providerAvailable=true;c.automaticApproved=ddcPackAutomaticApproved(pack,build,drivers);c.qualification=c.automaticApproved?"APPROVED matrix receipt matches current OS/driver/hardware":"NOT RUN / current OS-driver-hardware combination has no admitted physical receipt";
    }
    result.candidates.push_back(std::move(c));
  };
  auto sdk=[&](const std::filesystem::path &root,const std::string &origin){add(root/"Lib/x64/Release/emacepl.dll",origin);add(root/"libraries/emacepl/bin/x64/Release/emacepl.dll",origin);add(root/"emacepl.dll",origin);};
  auto choose=[&](const std::vector<const DdcRuntimeCandidate *> &values){if(values.empty())return false;const auto &first=*values.front();for(const auto *c:values)if(c->sha256!=first.sha256)return false;result.path=first.path;return true;};
  auto configuredSdk=[&](const std::filesystem::path &root,const std::string &origin){
    result.explicitSelection=true;sdk(root,origin);std::vector<const DdcRuntimeCandidate *> usable;for(const auto &c:result.candidates)if(mayProbe?c.usable:c.error.empty())usable.push_back(&c);
    if(result.candidates.empty()){result.path=root;result.error="Configured DDC SDK folder contains no x64 emacepl.dll in a supported layout: "+utf8(root);}
    else if(usable.empty()){result.path=result.candidates.front().path;result.error="Configured DDC SDK has no usable receive runtime: "+utf8(root);for(const auto &c:result.candidates)result.error+="; "+utf8(c.path)+": "+c.error;}
    else if(!choose(usable)){result.path=usable.front()->path;result.error="Configured DDC SDK folder contains conflicting receive runtimes. Select the exact emacepl.dll in Device Setup: "+utf8(root);}
  };
  if(!overridePath.empty()){result.explicitSelection=true;std::error_code ec;if(std::filesystem::is_directory(overridePath,ec))configuredSdk(overridePath,"manual SDK folder");else{result.path=overridePath;add(overridePath,"manual override");if(result.candidates.empty())result.error="Selected DDC runtime does not exist: "+utf8(overridePath);else if(!result.candidates[0].error.empty())result.error="Selected DDC runtime is incompatible: "+result.candidates[0].error;else result.path=result.candidates[0].path;}return result;}
  wchar_t env[32768]{};const auto rootLength=GetEnvironmentVariableW(L"DDC_SDK_ROOT",env,32768);
  if(rootLength){result.explicitSelection=true;if(rootLength>=32768)result.error="DDC_SDK_ROOT exceeds the supported Windows path length";else configuredSdk(env,"DDC_SDK_ROOT");return result;}
  std::vector<std::filesystem::path> roots{L"C:/DDC"};for(const auto *name:{L"ProgramFiles",L"ProgramFiles(x86)"})if(GetEnvironmentVariableW(name,env,32768))roots.push_back(std::filesystem::path(env)/L"DDC");
  for(const REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY})for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER}){
    HKEY uninstall{};if(RegOpenKeyExW(hive,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&uninstall)!=ERROR_SUCCESS)continue;wchar_t sub[512]{};DWORD length=512;
    for(DWORD i=0;RegEnumKeyExW(uninstall,i,sub,&length,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS;++i,length=512){HKEY item{};if(RegOpenKeyExW(uninstall,sub,0,KEY_READ,&item)!=ERROR_SUCCESS)continue;wchar_t display[1024]{},location[32768]{};DWORD bytes=sizeof(display),type{};RegQueryValueExW(item,L"DisplayName",nullptr,&type,reinterpret_cast<LPBYTE>(display),&bytes);const auto title=lower(utf8(display));if(title.find("acextreme")!=std::string::npos){bytes=sizeof(location);if(RegQueryValueExW(item,L"InstallLocation",nullptr,&type,reinterpret_cast<LPBYTE>(location),&bytes)==ERROR_SUCCESS&&location[0])roots.emplace_back(location);}RegCloseKey(item);}RegCloseKey(uninstall);
  }
  for(const auto &root:roots){sdk(root,"installed SDK");std::error_code ec;unsigned examined=0;for(std::filesystem::directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end&&!ec&&examined<256;it.increment(ec),++examined)if(it->is_directory(ec))sdk(it->path(),"installed SDK");}
  for(const auto &pack:packs)if(!pack.runtimePath.empty())add(pack.runtimePath,"provider pack");
  wchar_t systemDirectory[32768]{};if(GetSystemDirectoryW(systemDirectory,32768))add(std::filesystem::path(systemDirectory)/"emacepl.dll","system runtime");
  wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);add(std::filesystem::path(executable).parent_path()/"emacepl.dll","packaged runtime");
#ifdef AEROTRACE_DDC_RUNTIME
  add(std::filesystem::path(AEROTRACE_DDC_RUNTIME),"build SDK");
#endif
  std::vector<const DdcRuntimeCandidate *> approved;for(const auto &c:result.candidates)if(c.usable&&c.error.empty()&&c.providerAvailable&&c.automaticApproved)approved.push_back(&c);
  if(!choose(approved)){
    if(!approved.empty())result.error="Multiple approved runtime/provider pairs require an explicit SDK selection";
    else if(!result.candidates.empty())result.error=mayProbe?"Compatibility connection available when an exact software-verified pack is selected; physical qualification NOT RUN. Automatic production selection requires an approved OS/driver/hardware receipt.":"SDK metadata identified; runtime/provider readiness has not been probed";
    else result.error="No DDC runtime detected. Install a licensed SDK/provider pack or select emacepl.dll in Device Setup.";
    if(!result.candidates.empty()){
      auto candidate=std::find_if(result.candidates.begin(),result.candidates.end(),[](const auto &c){return c.usable&&c.error.empty()&&c.providerAvailable;});
      if(candidate==result.candidates.end())candidate=std::find_if(result.candidates.begin(),result.candidates.end(),[](const auto &c){return c.usable&&c.error.empty();});
      if(candidate==result.candidates.end())candidate=std::find_if(result.candidates.begin(),result.candidates.end(),[](const auto &c){return c.error.empty();});
      result.path=candidate==result.candidates.end()?result.candidates.front().path:candidate->path;
    }
  }
#else
  (void)overridePath;(void)probe;result.error="DDC runtime discovery requires Windows";
#endif
  return result;
}
}
