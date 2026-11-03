#include "provider_abi.h"
#include "source.hpp"
#include "ddc_runtime.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <windows.h>
namespace aero {std::unique_ptr<CaptureSource> makeVendorDdcSource();}
namespace {
struct State {
  std::unique_ptr<aero::CaptureSource> source;
  AtCallbacks callbacks{};aero::SourceBatch batch;
  std::vector<AtRecord> records;std::vector<AtPacket> packets;AtMetrics last{};bool outstanding{};
};
int error(AtError *out,const char *operation,const std::string &detail) noexcept {
  if(out){*out={};out->size=sizeof(*out);out->category=1;out->code=1;std::strncpy(out->operation,operation,sizeof(out->operation)-1);std::strncpy(out->detail,detail.c_str(),sizeof(out->detail)-1);}return 1;
}
template<class F> int protect(AtError *e,const char *op,F f) noexcept {try{return f();}catch(const std::exception &x){return error(e,op,x.what());}catch(...){return error(e,op,"Unknown provider exception");}}
aero::CaptureConfig configFor(State *s,const AtConfig *input) {
  aero::CaptureConfig c;c.runtime=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(input->runtime_utf8)));
  for(unsigned k=0;k<4;++k)c.enabled[k]=bool(input->enabled_mask&(1u<<k));
  for(unsigned k=0;k<2;++k)c.logicalDevices[k]=input->logical_devices[k];
  c.diagnostic=[s](const std::string &op,int code,const std::string &detail){if(s->callbacks.diagnostic)s->callbacks.diagnostic(s->callbacks.context,op.c_str(),code,detail.c_str());};
  c.deviceState=[s](unsigned ch,aero::DevicePhase p,const std::string &detail){if(s->callbacks.device_state)s->callbacks.device_state(s->callbacks.context,ch,uint32_t(p),detail.c_str());};
  c.cancelRequested=[s]{return s->callbacks.cancelled&&s->callbacks.cancelled(s->callbacks.context);};return c;
}
int AT_CALL openProvider(const AtConfig *c,void **out,AtError *e) {return protect(e,"open",[&]{
  if(!out||!c||c->size!=sizeof(*c)||c->callbacks.size!=sizeof(AtCallbacks)||!c->runtime_utf8)return error(e,"open","Invalid ABI configuration");
  *out=nullptr;auto state=std::make_unique<State>();state->callbacks=c->callbacks;
#ifdef AEROTRACE_PROVIDER_FIXTURE
  const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(c->runtime_utf8)));
  const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  const bool fixture=module&&GetProcAddress(module,"aeroTestStartCount");if(module)FreeLibrary(module);
  if(!fixture)return error(e,"open","Test-only provider refuses a non-fixture runtime");
#else
  if(aero::ddcRuntimeSha256(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(c->runtime_utf8))))!=AEROTRACE_PROVIDER_RUNTIME_HASH)
    return error(e,"open","Provider required: runtime fingerprint is outside this provider's verified build evidence");
#endif
  state->source=aero::makeVendorDdcSource();const auto message=state->source->open(configFor(state.get(),c));
  if(!message.empty()){state->source->close();return error(e,"open",message);}*out=state.release();return 0;
});}
int AT_CALL update(void *h,const AtCallbacks *c,AtError *e){return protect(e,"callbacks",[&]{if(!h||!c||c->size!=sizeof(*c))return error(e,"callbacks","Invalid callbacks");static_cast<State *>(h)->callbacks=*c;return 0;});}
int AT_CALL start(void *h,AtError *e){return protect(e,"start",[&]{if(!h)return error(e,"start","No provider handle");auto &s=*static_cast<State *>(h);const auto m=s.source->startMonitoring();return m.empty()?0:error(e,"start",m);});}
int batch(void *h,AtBatch *out,AtError *e,bool tail){return protect(e,tail?"drain":"poll",[&]{
  if(!h||!out||out->size!=sizeof(*out))return error(e,"poll","Invalid batch descriptor");auto &s=*static_cast<State *>(h);
  if(s.outstanding)return error(e,"poll","Previous provider batch has not been released");
  s.batch={};s.records.clear();s.packets.clear();const auto began=std::chrono::steady_clock::now();
  const auto message=tail?s.source->finish(s.batch):s.source->poll(s.batch,{});
  if(s.batch.records.size()>AT_PROVIDER_MAX_RECORDS||s.batch.packets.size()>AT_PROVIDER_MAX_PACKETS)return error(e,"poll","Provider batch exceeds ABI bounds");
  s.records.reserve(s.batch.records.size());for(const auto &r:s.batch.records){if(r.words.size()>32)return error(e,"poll","Invalid word count");AtRecord v{};
    v.id=r.id;v.timestamp_ns=r.timestampNs;v.device_ticks=r.deviceTicks;v.host_monotonic_ns=r.hostMonotonicNs;v.tick_resolution_ns=r.tickResolutionNs;
    v.command=r.commandWord;v.status=r.statusWord;v.flags=r.flags;v.vendor_status=r.vendorStatus;v.command2=r.commandWord2;v.status2=r.statusWord2;v.validity=r.validity;v.message_type=r.messageType;
    v.channel=r.channel;v.bus=r.bus;v.source=r.source;v.word_count=uint16_t(r.words.size());std::copy(r.words.begin(),r.words.end(),v.words);s.records.push_back(v);}
  for(const auto &p:s.batch.packets){if(p.bytes.size()>65536)return error(e,"poll","Raw packet exceeds bounded size");s.packets.push_back({p.channel,uint32_t(p.chapter10),uint32_t(p.bytes.size()),p.bytes.data()});}
  s.last={};s.last.lost=s.batch.hardwareLost;s.last.overflows=s.batch.hardwareOverflows;
  for(unsigned i=0;i<2;++i){s.last.channel_lost[i]=s.batch.channelLost[i];s.last.channel_overflows[i]=s.batch.channelOverflows[i];}
  s.last.poll_ns=s.batch.pollNs;s.last.packet_processing_ns=s.batch.packetProcessingNs;
  if(!s.last.poll_ns&&!s.last.packet_processing_ns)s.last.poll_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-began).count();
  *out={};out->size=sizeof(*out);out->record_count=uint32_t(s.records.size());out->packet_count=uint32_t(s.packets.size());out->records=s.records.data();out->packets=s.packets.data();out->metrics=s.last;out->release_token=&s;s.outstanding=true;
  return message.empty()?0:error(e,tail?"drain":"poll",message);
});}
int AT_CALL poll(void *h,AtBatch *b,AtError *e){return batch(h,b,e,false);}
int AT_CALL drain(void *h,AtBatch *b,AtError *e){return batch(h,b,e,true);}
int AT_CALL metrics(void *h,AtMetrics *m,AtError *e){if(!h||!m)return error(e,"metrics","Invalid metrics arguments");*m=static_cast<State *>(h)->last;return 0;}
void AT_CALL release(void *h,void *token){if(h&&token==h){auto &s=*static_cast<State *>(h);s.outstanding=false;s.batch={};s.records.clear();s.packets.clear();}}
int AT_CALL close(void *h,AtError *e){return protect(e,"close",[&]{if(!h)return 0;std::unique_ptr<State> s(static_cast<State *>(h));s->source->close();const auto m=s->source->closeError();return m.empty()?0:error(e,"close",m);});}
const AtProviderApi api{sizeof(AtProviderApi),AT_PROVIDER_ABI,
#ifdef AEROTRACE_PROVIDER_FIXTURE
  AT_PROVIDER_FIXTURE,
#else
  0,
#endif
  3,AEROTRACE_PROVIDER_ID,AEROTRACE_PROVIDER_SDK,AEROTRACE_PROVIDER_HEADER_HASH,AEROTRACE_PROVIDER_RUNTIME_HASH,"software build verified; physical qualification NOT RUN",
  openProvider,update,start,poll,drain,metrics,release,close};
}
extern "C" AT_EXPORT const AtProviderApi *AT_CALL aerotrace_provider_get_api(uint32_t abi,uint32_t size){return abi==AT_PROVIDER_ABI&&size==sizeof(AtProviderApi)?&api:nullptr;}
