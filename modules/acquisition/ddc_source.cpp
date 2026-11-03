#include "source.hpp"
#include "ddc_runtime.hpp"
#include "providers/provider_abi.h"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
namespace aero { namespace {
std::atomic<bool> providerActive{};
std::string textPath(const std::filesystem::path &p){const auto s=p.u8string();return {s.begin(),s.end()};}
struct Module {
  HMODULE dll{};const AtProviderApi *api{};std::filesystem::path path;
  ~Module(){if(dll)FreeLibrary(dll);}
};
std::unique_ptr<Module> loadProvider(const std::filesystem::path &runtime){
  std::vector<std::filesystem::path> paths;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",env,32768);
  const bool explicitProvider=n>0;if(n>=32768)throw std::runtime_error("Provider path exceeds Windows path limit");
  if(explicitProvider){std::error_code ec;const std::filesystem::path chosen(env);if(std::filesystem::is_directory(chosen,ec)){for(std::filesystem::directory_iterator i(chosen,ec),end;i!=end&&!ec;i.increment(ec))if(i->path().extension()==".dll")paths.push_back(i->path());}else paths.push_back(chosen);}
  else {
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    std::vector<std::filesystem::path> roots{std::filesystem::path(exe).parent_path()/"providers"};
#ifdef AEROTRACE_PROVIDER_DIRECTORY
    roots.emplace_back(AEROTRACE_PROVIDER_DIRECTORY);
#endif
    for(const auto &root:roots){std::error_code ec;for(std::filesystem::directory_iterator i(root,ec),end;i!=end&&!ec;i.increment(ec))if(i->path().extension()==".dll")paths.push_back(i->path());}
  }
  const auto fingerprint=ddcRuntimeSha256(runtime);if(fingerprint.empty())throw std::runtime_error("Cannot fingerprint selected DDC runtime");
  std::unique_ptr<Module> selected;std::string rejection;
  for(const auto &path:paths){
    auto module=std::make_unique<Module>();module->path=std::filesystem::absolute(path);
    module->dll=LoadLibraryExW(module->path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module->dll){rejection="Provider DLL could not load (Windows error "+std::to_string(GetLastError())+")";continue;}
    const auto get=reinterpret_cast<AtGetProviderApi>(GetProcAddress(module->dll,"aerotrace_provider_get_api"));
    if(!get){rejection="Provider DLL lacks aerotrace_provider_get_api";continue;}
    const auto api=get(AT_PROVIDER_ABI,sizeof(AtProviderApi));
    if(!api||api->size!=sizeof(AtProviderApi)||api->abi!=AT_PROVIDER_ABI){rejection="Provider ABI mismatch";continue;}
    if(!api->provider_id||!api->sdk_identity||!api->header_sha256||!api->runtime_sha256||!api->qualification||!api->open||!api->update_callbacks||!api->start||!api->poll||!api->drain||!api->metrics||!api->release_batch||!api->close){rejection="Incomplete provider interface";continue;}
    if(api->flags&AT_PROVIDER_FIXTURE){if(!explicitProvider){rejection="Test-only provider is never automatically selected";continue;}}
    else if(fingerprint!=api->runtime_sha256){rejection="Runtime fingerprint has no matching provider evidence";continue;}
    module->api=api;
    if(selected){if(std::string(selected->api->provider_id)==api->provider_id&&ddcRuntimeSha256(selected->path)==ddcRuntimeSha256(module->path))continue;throw std::runtime_error("Ambiguous DDC providers; select the exact provider using AEROTRACE_DDC_PROVIDER");}
    selected=std::move(module);
  }
  if(!selected)throw std::runtime_error("Provider required for selected SDK runtime. Build a provider against that SDK's headers and qualify it. "+rejection);
  return selected;
}
class ProviderSource final: public CaptureSource {
  std::unique_ptr<Module> module;void *handle{};CaptureConfig config;bool owns{};std::string closeFailure;
  static void AT_CALL diagnostic(void *p,const char *op,int32_t code,const char *detail) noexcept {try{auto &c=static_cast<ProviderSource *>(p)->config;if(c.diagnostic)c.diagnostic(op?op:"",code,detail?detail:"");}catch(...) {}}
  static void AT_CALL device(void *p,uint32_t ch,uint32_t phase,const char *detail) noexcept {try{auto &c=static_cast<ProviderSource *>(p)->config;if(c.deviceState&&ch<2&&phase<=uint32_t(DevicePhase::Failed))c.deviceState(ch,DevicePhase(phase),detail?detail:"");}catch(...) {}}
  static int32_t AT_CALL cancelled(void *p) noexcept {const auto &c=static_cast<ProviderSource *>(p)->config;return c.stopToken.stop_requested();}
  AtCallbacks callbacks(){return {sizeof(AtCallbacks),this,diagnostic,device,cancelled};}
  static std::string failure(int code,const AtError &e){return code?(e.operation[0]?std::string(e.operation)+": ":"")+std::string(e.detail):std::string();}
  std::string receive(SourceBatch &out,bool tail){
    if(!handle)return "No active provider";AtBatch batch{};batch.size=sizeof(batch);AtError error{};
    const auto code=(tail?module->api->drain:module->api->poll)(handle,&batch,&error);
    struct Release {ProviderSource *s;void *token;~Release(){if(token)s->module->api->release_batch(s->handle,token);}} release{this,batch.release_token};
    if(batch.record_count>AT_PROVIDER_MAX_RECORDS||batch.packet_count>AT_PROVIDER_MAX_PACKETS||(batch.record_count&&!batch.records)||(batch.packet_count&&!batch.packets))return "Provider returned invalid batch bounds";
    out.records.reserve(out.records.size()+batch.record_count);
    for(uint32_t i=0;i<batch.record_count;++i){const auto &v=batch.records[i];if(v.word_count>32||v.channel<1||v.channel>2||v.bus>1||v.source>2)return "Provider returned invalid normalized record";Record r;
      r.id=v.id;r.timestampNs=v.timestamp_ns;r.deviceTicks=v.device_ticks;r.hostMonotonicNs=v.host_monotonic_ns;r.tickResolutionNs=v.tick_resolution_ns;
      r.commandWord=v.command;r.statusWord=v.status;r.flags=v.flags;r.vendorStatus=v.vendor_status;r.commandWord2=v.command2;r.statusWord2=v.status2;r.validity=v.validity;r.messageType=v.message_type;r.channel=v.channel;r.bus=v.bus;r.source=v.source;r.words.assign(v.words,v.words+v.word_count);out.records.push_back(std::move(r));}
    for(uint32_t i=0;i<batch.packet_count;++i){const auto &p=batch.packets[i];if(p.channel<1||p.channel>2||p.size>65536||(p.size&&!p.data))return "Provider returned invalid raw packet";SourceBatch::Packet raw;raw.channel=p.channel;raw.chapter10=bool(p.chapter10);if(p.size)raw.bytes.assign(p.data,p.data+p.size);out.packets.push_back(std::move(raw));}
    out.hardwareLost=batch.metrics.lost;out.hardwareOverflows=batch.metrics.overflows;out.pollNs=batch.metrics.poll_ns;out.packetProcessingNs=batch.metrics.packet_processing_ns;
    for(unsigned i=0;i<2;++i){out.channelLost[i]=batch.metrics.channel_lost[i];out.channelOverflows[i]=batch.metrics.channel_overflows[i];}
    return failure(code,error);
  }
public:
  ~ProviderSource() override {close();}
  std::string open(const CaptureConfig &value) override {
    close();closeFailure.clear();config=value;
    try {if(providerActive.exchange(true))return "Another SDK provider already owns the process connection";owns=true;
      const auto selected=selectDdcRuntime(config.runtime);if(!selected.error.empty())throw std::runtime_error(selected.error);
      module=loadProvider(selected.path);const auto runtime=textPath(selected.path);AtConfig c{};c.size=sizeof(c);c.runtime_utf8=runtime.c_str();c.callbacks=callbacks();
      for(unsigned i=0;i<4;++i)if(config.enabled[i])c.enabled_mask|=1u<<i;for(unsigned i=0;i<2;++i)c.logical_devices[i]=config.logicalDevices[i];
      if(config.diagnostic)config.diagnostic("provider_selected",0,std::string(module->api->provider_id)+"; SDK="+module->api->sdk_identity+"; headers_sha256="+module->api->header_sha256+"; runtime_sha256="+ddcRuntimeSha256(selected.path)+"; "+module->api->qualification);
      AtError e{};if(auto code=module->api->open(&c,&handle,&e))throw std::runtime_error(failure(code,e));return {};
    }catch(const std::exception &e){const std::string m=e.what();close();return m;}
  }
  void updateCallbacks(const CaptureConfig &value) override {config.diagnostic=value.diagnostic;config.deviceState=value.deviceState;config.stopToken=value.stopToken;if(handle){const auto c=callbacks();AtError e{};module->api->update_callbacks(handle,&c,&e);}}
  std::string startMonitoring() override {AtError e{};return handle?failure(module->api->start(handle,&e),e):"No active provider";}
  std::string poll(SourceBatch &b,std::stop_token) override{return receive(b,false);}
  std::string finish(SourceBatch &b) override{return receive(b,true);}
  void close() override {if(handle){AtError e{};const auto code=module->api->close(handle,&e);closeFailure=failure(code,e);handle=nullptr;}module.reset();if(owns){owns=false;providerActive=false;}}
  std::string closeError() const override{return closeFailure;}
};
}
std::unique_ptr<CaptureSource> makeDdcSource(){return std::make_unique<ProviderSource>();}
DdcRuntimeInfo inspectDdcRuntime(const std::filesystem::path &path){try{const auto s=selectDdcRuntime(path);if(!s.error.empty())return {false,s.error,path};auto p=loadProvider(s.path);return {true,std::string("Receive provider available: ")+p->api->provider_id+"; "+p->api->qualification,s.path};}catch(const std::exception &e){return {false,e.what(),path};}}
}
#else
namespace aero {namespace {class Unavailable final:public CaptureSource{std::string open(const CaptureConfig &)override{return "DDC providers require Windows x64";}std::string poll(SourceBatch &,std::stop_token)override{return "DDC unavailable";}void close()override{}};}
std::unique_ptr<CaptureSource> makeDdcSource(){return std::make_unique<Unavailable>();}
DdcRuntimeInfo inspectDdcRuntime(const std::filesystem::path &p){return {false,"DDC providers require Windows x64",p};}}
#endif
