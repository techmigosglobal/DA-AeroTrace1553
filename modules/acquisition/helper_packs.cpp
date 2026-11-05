#include "helper_packs.hpp"
#include "helper_windows.hpp"
#include "provider_pack.hpp"
#include <set>
#ifdef _WIN32
#include <cfgmgr32.h>
#include <setupapi.h>
#endif
namespace aero {
DdcHelperPack readDdcHelperPack(const std::filesystem::path &manifest){
  DdcHelperPack p;p.manifest=manifest;
  try{
    const auto bytes=std::filesystem::file_size(manifest);if(bytes>1024*1024)throw std::runtime_error("Helper pack exceeds 1 MiB");std::ifstream f(manifest,std::ios::binary);std::string text((std::istreambuf_iterator<char>(f)),{});if(text.size()!=bytes)throw std::runtime_error("Helper manifest changed during read");auto j=provider_pack_detail::Parser(text).parse();
    if(j.at("schema").string()!="aerotrace.ddc-helper-pack.v1"||j.at("architecture").string()!="x86"||j.at("protocol").integer()!=1)throw std::runtime_error("Unsupported helper pack schema, architecture or protocol");
    p.id=j.at("id").string();if(p.id.empty()||p.id.size()>128||!std::all_of(p.id.begin(),p.id.end(),[](unsigned char c){return std::isalnum(c)||c=='.'||c=='_'||c=='-';}))throw std::runtime_error("Invalid helper pack ID");
    const auto &h=j.at("helper"),&r=j.at("runtime"),&s=j.at("sdk");p.helperPath=provider_pack_detail::confined(manifest.parent_path(),h.at("path").string());p.runtimePath=provider_pack_detail::confined(manifest.parent_path(),r.at("path").string());p.helperSha256=provider_pack_detail::hash(h.at("sha256"));p.runtimeSha256=provider_pack_detail::hash(r.at("sha256"));p.headerSha256=provider_pack_detail::hash(s.at("header_sha256"));p.sdkIdentity=s.at("identity").string();
    const auto &receipts=j.at("receipts");p.softwareVerified=receipts.at("compiled").flag()&&receipts.at("contract_tested").flag()&&receipts.at("runtime_inspected").flag();if(!p.softwareVerified)throw std::runtime_error("Helper software verification receipts are incomplete");
    for(const auto &mode:j.at("receive_modes").array){auto name=mode.string();if(name!="Auto"&&name!="MTI"&&name!="LegacyMT")throw std::runtime_error("Unknown helper receive mode");p.modes.push_back(name);}if(p.modes.empty()||p.modes.size()>3)throw std::runtime_error("Invalid helper receive modes");
    if(auto i=j.object.find("fixture");i!=j.object.end())p.fixture=i->second.flag();
#ifdef _WIN32
    if(!helper::x86Image(p.helperPath)||!helper::x86Image(p.runtimePath))throw std::runtime_error("Helper executable and runtime must both be x86 PE images");if(helper::hashFile(p.helperPath)!=p.helperSha256||helper::hashFile(p.runtimePath)!=p.runtimeSha256)throw std::runtime_error("Helper executable/runtime SHA-256 mismatch");
    // Physical approval requires an exact present/started device + driver binding.
    const auto &matrix=j.at("compatibility").at("matrix");if(matrix.kind!=provider_pack_detail::Json::Array||matrix.array.size()>128)throw std::runtime_error("Invalid helper qualification matrix");
    HKEY versionKey{};DWORD build{},n=sizeof(build),kind{};wchar_t buildText[64]{};if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",0,KEY_READ|KEY_WOW64_64KEY,&versionKey)==ERROR_SUCCESS){n=sizeof(buildText);if(RegQueryValueExW(versionKey,L"CurrentBuildNumber",nullptr,&kind,reinterpret_cast<BYTE*>(buildText),&n)==ERROR_SUCCESS)build=wcstoul(buildText,nullptr,10);RegCloseKey(versionKey);}
    auto set=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);if(set!=INVALID_HANDLE_VALUE){SP_DEVINFO_DATA device{sizeof(device)};for(DWORD index=0;SetupDiEnumDeviceInfo(set,index,&device);++index){ULONG status{},problem{};if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)!=CR_SUCCESS||problem||!(status&DN_STARTED))continue;wchar_t ids[8192]{};DWORD type{},size=sizeof(ids);if(!SetupDiGetDeviceRegistryPropertyW(set,&device,SPDRP_HARDWAREID,&type,reinterpret_cast<BYTE*>(ids),size,&size))continue;auto key=SetupDiOpenDevRegKey(set,&device,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);if(key==INVALID_HANDLE_VALUE)continue;wchar_t driver[256]{};size=sizeof(driver);const bool haveDriver=RegQueryValueExW(key,L"DriverVersion",nullptr,&type,reinterpret_cast<BYTE*>(driver),&size)==ERROR_SUCCESS;RegCloseKey(key);if(!haveDriver)continue;for(const auto &q:matrix.array){if(!q.at("physical_qualified").flag())continue;auto id=q.at("hardware_id").string();if(id.empty()||id.find_first_of("*?")!=std::string::npos||q.at("receipt").string().empty())throw std::runtime_error("Invalid helper qualification receipt");if(build<q.at("windows_min_build").integer()||build>q.at("windows_max_build").integer()||helper::utf8(driver)!=q.at("driver_version").string())continue;for(const wchar_t *v=ids;*v;v+=wcslen(v)+1)if(_wcsicmp(v,helper::wide(id).c_str())==0)p.automaticApproved=true;}}SetupDiDestroyDeviceInfoList(set);}
#else
    throw std::runtime_error("DDC helper packs require Windows");
#endif
  }catch(const std::exception &e){p.error=e.what();p.softwareVerified=false;p.automaticApproved=false;}
  return p;
}
std::vector<DdcHelperPack> discoverDdcHelperPacks(){
  std::vector<DdcHelperPack> out;
#ifdef _WIN32
  std::vector<std::filesystem::path> roots{helper::executablePath().parent_path()/"helpers/packs"};wchar_t userRoot[32768]{};if(GetEnvironmentVariableW(L"LOCALAPPDATA",userRoot,32768))roots.push_back(std::filesystem::path(userRoot)/"AeroTrace/AeroTrace1553/helpers/packs");wchar_t overrideRoot[32768]{};if(GetEnvironmentVariableW(L"AEROTRACE_DDC_HELPER_PACKS",overrideRoot,32768))roots.insert(roots.begin(),overrideRoot);std::set<std::filesystem::path> seen;
  for(const auto &root:roots){std::error_code ec;if(std::filesystem::is_regular_file(root,ec)){if(seen.insert(root).second)out.push_back(readDdcHelperPack(root));continue;}unsigned count=0;for(std::filesystem::directory_iterator i(root,std::filesystem::directory_options::skip_permission_denied,ec),end;i!=end&&!ec&&count<128;i.increment(ec),++count){auto path=i->path()/"helper-pack.json";if(std::filesystem::is_regular_file(path,ec)&&seen.insert(path).second){auto p=readDdcHelperPack(path);if(!p.fixture)out.push_back(std::move(p));}}}
#endif
  return out;
}
}
