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
std::string textPath(const std::filesystem::path &p){const auto s=p.u8string();return {s.begin(),s.end()};}
struct Module {
  HMODULE dll{};const AtProviderApi *api{};const AtProviderApiV2 *api2{};std::filesystem::path path;
  ~Module(){if(dll)FreeLibrary(dll);}
};
std::unique_ptr<Module> loadProvider(const std::filesystem::path &runtime,const std::filesystem::path &packProvider={},const std::filesystem::path &packManifest={}){
  std::vector<std::filesystem::path> paths;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",env,32768);
  const bool explicitProvider=n>0;if(n>=32768)throw std::runtime_error("Provider path exceeds Windows path limit");
  if(explicitProvider){std::error_code ec;const std::filesystem::path chosen(env);if(std::filesystem::is_directory(chosen,ec)){for(std::filesystem::directory_iterator i(chosen,ec),end;i!=end&&!ec;i.increment(ec))if(i->path().extension()==".dll")paths.push_back(i->path());}else paths.push_back(chosen);}
  else if(!packProvider.empty()) paths.push_back(packProvider);
  else {
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto application=std::filesystem::path(exe).parent_path();
    std::vector<std::filesystem::path> roots{application/"providers"};
#ifdef AEROTRACE_PROVIDER_DIRECTORY
    std::error_code deploymentError;
    if(!std::filesystem::is_regular_file(application/"deployment.json",deploymentError))roots.emplace_back(AEROTRACE_PROVIDER_DIRECTORY);
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
    if(!explicitProvider&&!packManifest.empty()){
      const auto pack=readDdcProviderPack(packManifest);
      if(!pack.error.empty()||ddcRuntimeSha256(module->path)!=pack.providerSha256||fingerprint!=pack.runtimeSha256||
         std::string(api->provider_id)!=pack.id||std::string(api->sdk_identity)!=pack.sdkIdentity||std::string(api->header_sha256)!=pack.headerSha256){rejection="Provider pack receipt does not match the loaded provider identity";continue;}
    }
    module->api=api;
    const auto api2=reinterpret_cast<const AtProviderApiV2 *>(get(AT_PROVIDER_ABI_V2,sizeof(AtProviderApiV2)));
    if(api2){if(api2->size!=sizeof(AtProviderApiV2)||api2->abi!=AT_PROVIDER_ABI_V2||api2->v1!=api||!api2->open||!api2->stop||!api2->drain_next||!api2->last_error||!api2->close){rejection="Provider ABI v2 table is incomplete";continue;}module->api2=api2;}
    if(selected){if(std::string(selected->api->provider_id)==api->provider_id&&ddcRuntimeSha256(selected->path)==ddcRuntimeSha256(module->path))continue;throw std::runtime_error("Ambiguous DDC providers; select the exact provider using AEROTRACE_DDC_PROVIDER");}
    selected=std::move(module);
  }
  if(!selected)throw std::runtime_error("Provider required for selected SDK runtime. Build a provider against that SDK's headers and qualify it. "+rejection);
  return selected;
}
class ProviderSource final: public CaptureSource {
  std::unique_ptr<Module> module;void *handle{};CaptureConfig config;bool owns{};std::string closeFailure;CaptureFailure lastError;CaptureIdentity resolved;
  static void AT_CALL diagnostic(void *p,const char *op,int32_t code,const char *detail) noexcept {try{auto &c=static_cast<ProviderSource *>(p)->config;if(c.diagnostic)c.diagnostic(op?op:"",code,detail?detail:"");}catch(...) {}}
  static void AT_CALL device(void *p,uint32_t ch,uint32_t phase,const char *detail) noexcept {try{auto &c=static_cast<ProviderSource *>(p)->config;if(c.deviceState&&ch<2&&phase<=uint32_t(DevicePhase::Failed))c.deviceState(ch,DevicePhase(phase),detail?detail:"");}catch(...) {}}
  static int32_t AT_CALL cancelled(void *p) noexcept {const auto &c=static_cast<ProviderSource *>(p)->config;return c.stopToken.stop_requested();}
  AtCallbacks callbacks(){return {sizeof(AtCallbacks),this,diagnostic,device,cancelled};}
  static std::string failure(int code,const AtError &e){return code?(e.operation[0]?std::string(e.operation)+": ":"")+std::string(e.detail):std::string();}
  std::string invalidBatch(const char *detail){lastError={};lastError.category=CaptureFailureCategory::Integrity;lastError.stage="provider_batch";lastError.operation="receive";lastError.detail=detail;lastError.recovery={"Preserve the incomplete recording and collect diagnostics"};return lastError.detail;}
  std::string typedFailure(int code,const AtErrorV2 &e){
    if(!code)return {};lastError={};lastError.category=e.category<=uint32_t(CaptureFailureCategory::Unknown)?CaptureFailureCategory(e.category):CaptureFailureCategory::Unknown;
    if(lastError.category==CaptureFailureCategory::None)lastError.category=CaptureFailureCategory::Unknown;
    lastError.code=e.code;lastError.channel=e.channel;lastError.logicalDevice=e.logical_device;lastError.windowsCode=e.windows_code;lastError.supplementaryWindows=bool(e.supplementary_windows);lastError.cleanupFailed=bool(e.cleanup_failed);
    lastError.stage=std::string(e.stage,strnlen(e.stage,sizeof(e.stage)));lastError.operation=std::string(e.operation,strnlen(e.operation,sizeof(e.operation)));lastError.detail=std::string(e.detail,strnlen(e.detail,sizeof(e.detail)));
    if(lastError.cleanupFailed)closeFailure=lastError.detail;
    const std::string recovery(e.recovery,strnlen(e.recovery,sizeof(e.recovery)));for(std::size_t at=0;at<recovery.size();){const auto end=recovery.find('\n',at);lastError.recovery.push_back(recovery.substr(at,end==std::string::npos?std::string::npos:end-at));if(end==std::string::npos)break;at=end+1;}
    return lastError.detail.empty()?lastError.operation+" failed":lastError.detail;
  }
  std::string legacyFailure(int code,const AtError &e){if(!code)return {};if(module&&module->api2&&handle){AtErrorV2 typed{};module->api2->last_error(handle,&typed);if(typed.category!=uint32_t(CaptureFailureCategory::None)&&typed.detail[0])return typedFailure(code,typed);}lastError={};lastError.category=CaptureFailureCategory::Unknown;lastError.operation=e.operation;lastError.stage=e.operation;lastError.code=e.code;lastError.detail=failure(code,e);return lastError.detail;}
  std::string receive(SourceBatch &out,bool tail,bool incremental=false,bool *more=nullptr){
    if(!handle)return "No active provider";AtBatch batch{};batch.size=sizeof(batch);AtError error{};
    AtErrorV2 typed{};uint32_t pending{};const bool v2=incremental&&module->api2;
    const auto code=v2?module->api2->drain_next(handle,&batch,&pending,&typed):(tail?module->api->drain:module->api->poll)(handle,&batch,&error);
    if(more)*more=v2?bool(pending):false;
    struct Release {ProviderSource *s;void *token;~Release(){if(token)s->module->api->release_batch(s->handle,token);}} release{this,batch.release_token};
    if(batch.record_count>AT_PROVIDER_MAX_RECORDS||batch.packet_count>AT_PROVIDER_MAX_PACKETS||(batch.record_count&&!batch.records)||(batch.packet_count&&!batch.packets))return invalidBatch("Provider returned invalid batch bounds");
    out.records.reserve(out.records.size()+batch.record_count);
    for(uint32_t i=0;i<batch.record_count;++i){const auto &v=batch.records[i];if(v.word_count>32||v.channel<1||v.channel>2||v.bus>1||v.source>2)return invalidBatch("Provider returned invalid normalized record");Record r;
      r.id=v.id;r.timestampNs=v.timestamp_ns;r.deviceTicks=v.device_ticks;r.hostMonotonicNs=v.host_monotonic_ns;r.tickResolutionNs=v.tick_resolution_ns;
      r.commandWord=v.command;r.statusWord=v.status;r.flags=v.flags;r.vendorStatus=v.vendor_status;r.commandWord2=v.command2;r.statusWord2=v.status2;r.validity=v.validity;r.messageType=v.message_type;r.channel=v.channel;r.bus=v.bus;r.source=v.source;r.words.assign(v.words,v.words+v.word_count);out.records.push_back(std::move(r));}
    for(uint32_t i=0;i<batch.packet_count;++i){const auto &p=batch.packets[i];if(p.channel<1||p.channel>2||p.size>65536||(p.size&&!p.data))return invalidBatch("Provider returned invalid raw packet");SourceBatch::Packet raw;raw.channel=p.channel;raw.chapter10=bool(p.chapter10);if(p.size)raw.bytes.assign(p.data,p.data+p.size);out.packets.push_back(std::move(raw));}
    out.hardwareLost=batch.metrics.lost;out.hardwareOverflows=batch.metrics.overflows;out.pollNs=batch.metrics.poll_ns;out.packetProcessingNs=batch.metrics.packet_processing_ns;
    for(unsigned i=0;i<2;++i){out.channelLost[i]=batch.metrics.channel_lost[i];out.channelOverflows[i]=batch.metrics.channel_overflows[i];}
    return v2?typedFailure(code,typed):legacyFailure(code,error);
  }
public:
  ~ProviderSource() override {close();}
  std::string open(const CaptureConfig &value) override {
    close();if(!closeFailure.empty())return "Previous SDK cleanup failed: "+closeFailure;lastError={};resolved={};config=value;
    try {DdcRuntimeProbe probe;if(!probe.admitted()){lastError.category=CaptureFailureCategory::Ownership;lastError.stage="admission";lastError.detail="Another SDK provider already owns the process connection";return lastError.detail;}
      if(config.connectionPolicy==DdcConnectionPolicy::Compatibility&&config.runtime.empty())throw std::runtime_error("Compatibility connection requires selecting an exact verified runtime pack");
      const auto selected=selectDdcRuntime(config.runtime);if(!selected.error.empty())throw std::runtime_error(selected.error);
      const auto candidate=std::find_if(selected.candidates.begin(),selected.candidates.end(),[&](const auto &c){return c.path==selected.path;});
      if(selected.explicitSelection&&(candidate==selected.candidates.end()||!candidate->automaticApproved))config.connectionPolicy=DdcConnectionPolicy::Compatibility;
      module=loadProvider(selected.path,candidate==selected.candidates.end()?std::filesystem::path{}:candidate->providerPath,candidate==selected.candidates.end()?std::filesystem::path{}:candidate->packManifest);
      resolved.backend="NativeX64";resolved.receiveMode=config.receiveMode==DdcReceiveMode::MTI?"MTI":config.receiveMode==DdcReceiveMode::LegacyMT?"LegacyMT":"Auto";
      resolved.runtime=selected.path;resolved.runtimeSha256=ddcRuntimeSha256(selected.path);resolved.provider=module->path;resolved.providerId=module->api->provider_id;resolved.sdkIdentity=module->api->sdk_identity;resolved.headerSha256=module->api->header_sha256;
      if(candidate!=selected.candidates.end())resolved.manifest=candidate->packManifest;
      resolved.qualification=candidate!=selected.candidates.end()&&candidate->automaticApproved?"APPROVED matrix receipt matches current OS/driver/hardware":"NOT RUN / compatibility connection";
      if(config.receiveMode!=DdcReceiveMode::Auto&&!module->api2){lastError.category=CaptureFailureCategory::UnsupportedMode;lastError.stage="provider_contract";lastError.detail="Explicit receive mode requires provider ABI v2; this provider supports Auto only";throw std::runtime_error(lastError.detail);}
      if(!probe.promoteToConnection())throw std::runtime_error("SDK readiness probe in progress; retry connection when the scan completes");owns=true;
      const auto runtime=textPath(selected.path);AtConfig c{};c.size=sizeof(c);c.runtime_utf8=runtime.c_str();c.callbacks=callbacks();
      for(unsigned i=0;i<4;++i)if(config.enabled[i])c.enabled_mask|=1u<<i;for(unsigned i=0;i<2;++i)c.logical_devices[i]=config.logicalDevices[i];
      if(config.diagnostic)config.diagnostic("provider_selected",0,std::string(module->api->provider_id)+"; SDK="+module->api->sdk_identity+"; headers_sha256="+module->api->header_sha256+"; runtime_sha256="+ddcRuntimeSha256(selected.path)+"; "+module->api->qualification);
      if(config.diagnostic)config.diagnostic("connection_policy",0,(selected.explicitSelection&&(candidate==selected.candidates.end()||!candidate->automaticApproved))?"Compatibility; physical qualification NOT RUN":config.connectionPolicy==DdcConnectionPolicy::Compatibility?"Compatibility":"QualifiedAutomatic");
      if(module->api2){AtConfigV2 c2{};c2.size=sizeof(c2);c2.receive_mode=uint32_t(config.receiveMode);c2.config=c;AtErrorV2 e{};if(auto code=module->api2->open(&c2,&handle,&e))throw std::runtime_error(typedFailure(code,e));}
      else {AtError e{};if(auto code=module->api->open(&c,&handle,&e))throw std::runtime_error(legacyFailure(code,e));}return {};
    }catch(const std::exception &e){const std::string m=e.what();if(!lastError){lastError.category=CaptureFailureCategory::RuntimeSelection;lastError.stage="runtime_provider_selection";lastError.operation="connect";lastError.detail=m;lastError.recovery={"Select an exact software-verified pack for a compatibility connection","Review runtime/provider details and collect diagnostics"};}close();return m;}
  }
  void updateCallbacks(const CaptureConfig &value) override {config.diagnostic=value.diagnostic;config.deviceState=value.deviceState;config.stopToken=value.stopToken;if(handle){const auto c=callbacks();AtError e{};module->api->update_callbacks(handle,&c,&e);}}
  std::string startMonitoring() override {AtError e{};return handle?legacyFailure(module->api->start(handle,&e),e):"No active provider";}
  std::string poll(SourceBatch &b,std::stop_token) override{return receive(b,false);}
  std::string finish(SourceBatch &b) override{return receive(b,true);}
  std::string stopMonitoring() override {if(!handle)return {};if(module->api2){AtErrorV2 e{};return typedFailure(module->api2->stop(handle,&e),e);}return {};}
  std::string drainNext(SourceBatch &out,bool &more) override {return receive(out,true,true,&more);}
  void close() override {if(handle){if(module->api2){AtErrorV2 e{};const auto code=module->api2->close(handle,&e);closeFailure=typedFailure(code,e);}else {AtError e{};const auto code=module->api->close(handle,&e);closeFailure=legacyFailure(code,e);}handle=nullptr;}module.reset();if(owns){owns=false;releaseDdcProviderConnection();}}
  std::string closeError() const override{return closeFailure;}
  CaptureFailure lastFailure() const override{return lastError;}
  CaptureIdentity identity() const override{return resolved;}
};
class BackendSource final:public CaptureSource {
  std::unique_ptr<CaptureSource> source;
public:
  std::string open(const CaptureConfig &c) override {
    if(source){source->close();if(!source->closeError().empty())return "Previous SDK cleanup failed: "+source->closeError();}
    source=c.backend==DdcBackend::HelperX86?makeDdcHelperSource():std::make_unique<ProviderSource>();return source->open(c);
  }
  std::string startMonitoring() override{return source?source->startMonitoring():"No connected source";}
  void updateCallbacks(const CaptureConfig &c) override{if(source)source->updateCallbacks(c);}
  std::string poll(SourceBatch &out,std::stop_token stop) override{return source?source->poll(out,stop):"No connected source";}
  std::string finish(SourceBatch &out) override{return source?source->finish(out):std::string{};}
  std::string stopMonitoring() override{return source?source->stopMonitoring():std::string{};}
  std::string drainNext(SourceBatch &out,bool &more) override{more=false;return source?source->drainNext(out,more):std::string{};}
  void close() override{if(source)source->close();}
  std::string closeError() const override{return source?source->closeError():std::string{};}
  CaptureFailure lastFailure() const override{return source?source->lastFailure():CaptureFailure{};}
  CaptureIdentity identity() const override{return source?source->identity():CaptureIdentity{};}
};
}
std::unique_ptr<CaptureSource> makeDdcSource(){return makeSdkOwnedSource(std::make_unique<BackendSource>());}
DdcRuntimeInfo inspectDdcRuntime(const std::filesystem::path &path,bool requestProbe){
  DdcRuntimeInfo info;info.path=path;DdcRuntimeProbe probe(requestProbe);
  try {
    const auto selected=selectDdcRuntime(path,requestProbe&&probe.admitted());info.path=selected.path;info.message=selected.error;info.probeBlocked=requestProbe&&!probe.admitted();
    const auto c=std::find_if(selected.candidates.begin(),selected.candidates.end(),[&](const auto &v){return v.path==selected.path;});
    if(c!=selected.candidates.end()){
      info.runtimeDetected=c->detected;info.runtimeLoadable=c->loadable;info.architecture=c->architecture;info.version=c->version;info.sha256=c->sha256;info.providerId=c->providerId;info.providerPath=c->providerPath;info.automaticApproved=c->automaticApproved;info.qualification=c->qualification;info.stage=c->stage;
      if(!c->error.empty()){info.message=c->error;return info;}
      if(!requestProbe||!probe.admitted()){info.stage=info.probeBlocked?"probe_blocked":"runtime_identified";info.message=info.probeBlocked?"Runtime identified; readiness probe deferred while the SDK connection is retained":"Runtime identified from metadata; loader and provider readiness not checked";return info;}
      if(c->usable){auto provider=loadProvider(c->path,c->providerPath,c->packManifest);info.providerAvailable=info.available=true;info.providerId=provider->api->provider_id;info.providerPath=provider->path;
        if(info.qualification.empty())info.qualification=provider->api->qualification;
        info.stage=(!selected.explicitSelection&&!info.automaticApproved)?"compatibility_available":"provider_ready";
        info.message="Receive provider available: "+info.providerId+"; "+info.qualification;
        if(!selected.error.empty())info.message+="; "+selected.error;
        return info;}
    }
    if(info.stage.empty())info.stage=info.runtimeDetected?"runtime_incompatible":"runtime_missing";
  }catch(const std::exception &e){info.message=e.what();info.stage=info.runtimeLoadable?"provider_missing":"runtime_incompatible";}
  return info;
}
}
#else
namespace aero {namespace {class Unavailable final:public CaptureSource{std::string open(const CaptureConfig &)override{return "DDC providers require Windows x64";}std::string poll(SourceBatch &,std::stop_token)override{return "DDC unavailable";}void close()override{}};}
std::unique_ptr<CaptureSource> makeDdcSource(){return std::make_unique<Unavailable>();}
DdcRuntimeInfo inspectDdcRuntime(const std::filesystem::path &p,bool){return {false,"DDC providers require Windows x64",p};}}
#endif
