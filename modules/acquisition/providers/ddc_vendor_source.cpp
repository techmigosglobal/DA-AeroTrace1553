#include "source.hpp"
#include "capture_time.hpp"
#include "packet_bounds.hpp"
#include "ddc_runtime.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>
#ifdef AEROTRACE_DDC
#include <windows.h>
#include "stdemace.h"
#endif

namespace aero {
#ifdef AEROTRACE_DDC
namespace {
std::string pathText(const std::filesystem::path &p){const auto value=p.u8string();return {value.begin(),value.end()};}
struct Runtime {
  HMODULE handle{};
  std::filesystem::path path;
#define DDC_ENTRY(name) decltype(&name) name##Fn{};
  DDC_ENTRY(aceGetLibVersion) DDC_ENTRY(aceErrorStr) DDC_ENTRY(aceInitialize)
  DDC_ENTRY(aceMTConfigure) DDC_ENTRY(aceMTInstallHBuf) DDC_ENTRY(aceMTStart) DDC_ENTRY(aceMTStop)
  DDC_ENTRY(aceMTGetHBufMsgsRaw) DDC_ENTRY(aceMTDecodeRawMsg)
  DDC_ENTRY(aceGetHwVersionInfo) DDC_ENTRY(aceFree) DDC_ENTRY(aceSetTimeTagRes) DDC_ENTRY(aceMTIConfigure)
  DDC_ENTRY(aceMTIStart) DDC_ENTRY(aceMTIStop) DDC_ENTRY(aceMTIGetCh10DataPkt)
  DDC_ENTRY(aceMTIFreeCh10DataPkt) DDC_ENTRY(aceMTIDecodeRawMsg) DDC_ENTRY(aceMtiGetMetrics)
#undef DDC_ENTRY
  explicit Runtime(const CaptureConfig &config) {
    if(config.runtime.empty() || config.runtimeSha256.empty())throw std::runtime_error("Vendor runtime requires an exact verified DLL and fingerprint");
    path=std::filesystem::absolute(config.runtime);
    if(ddcRuntimeSha256(path)!=config.runtimeSha256)throw std::runtime_error("Vendor runtime fingerprint changed after pack validation");
    std::ifstream image(path,std::ios::binary);std::uint16_t mz{},machine{};std::uint32_t offset{},signature{};
    image.read(reinterpret_cast<char *>(&mz),2);image.seekg(0x3c);image.read(reinterpret_cast<char *>(&offset),4);
    if(!image||offset>16*1024*1024)throw std::runtime_error("Invalid vendor runtime PE header");
    image.seekg(offset);image.read(reinterpret_cast<char *>(&signature),4);image.read(reinterpret_cast<char *>(&machine),2);
    const bool x86=sizeof(void *)==4;
    if(!image||mz!=0x5a4d||signature!=0x4550||machine!=(x86?0x14c:0x8664)||config.runtimeArchitecture!=(x86?"x86":"x64"))
      throw std::runtime_error("Vendor runtime architecture does not match the receiving process");
    handle=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!handle) throw std::runtime_error("Cannot load DDC emacepl.dll for this process (Windows error "+std::to_string(GetLastError())+"): "+pathText(path));
    try {
#define DDC_LOAD(name) name##Fn=reinterpret_cast<decltype(name##Fn)>(GetProcAddress(handle,#name));if(!name##Fn) throw std::runtime_error("DDC runtime lacks " #name);
      DDC_LOAD(aceGetLibVersion) DDC_LOAD(aceErrorStr) DDC_LOAD(aceInitialize)
      DDC_LOAD(aceGetHwVersionInfo) DDC_LOAD(aceFree) DDC_LOAD(aceSetTimeTagRes)
#undef DDC_LOAD
#define DDC_LOAD(name) name##Fn=reinterpret_cast<decltype(name##Fn)>(GetProcAddress(handle,#name));
      DDC_LOAD(aceMTConfigure) DDC_LOAD(aceMTInstallHBuf) DDC_LOAD(aceMTStart) DDC_LOAD(aceMTStop)
      DDC_LOAD(aceMTGetHBufMsgsRaw) DDC_LOAD(aceMTDecodeRawMsg)
      DDC_LOAD(aceMTIConfigure)
      DDC_LOAD(aceMTIStart) DDC_LOAD(aceMTIStop) DDC_LOAD(aceMTIGetCh10DataPkt)
      DDC_LOAD(aceMTIFreeCh10DataPkt) DDC_LOAD(aceMTIDecodeRawMsg) DDC_LOAD(aceMtiGetMetrics)
#undef DDC_LOAD
    } catch(...) { FreeLibrary(handle);handle=nullptr;throw; }
  }
  ~Runtime() { if(handle) FreeLibrary(handle); }
  void requireProtocol(bool legacy) const {
#define DDC_REQUIRE(name) if(!name##Fn)throw std::runtime_error("DDC runtime lacks " #name);
    if(legacy){DDC_REQUIRE(aceMTConfigure) DDC_REQUIRE(aceMTInstallHBuf) DDC_REQUIRE(aceMTStart) DDC_REQUIRE(aceMTStop) DDC_REQUIRE(aceMTGetHBufMsgsRaw) DDC_REQUIRE(aceMTDecodeRawMsg)}
    else {DDC_REQUIRE(aceMTIConfigure) DDC_REQUIRE(aceMTIStart) DDC_REQUIRE(aceMTIStop) DDC_REQUIRE(aceMTIGetCh10DataPkt) DDC_REQUIRE(aceMTIFreeCh10DataPkt) DDC_REQUIRE(aceMTIDecodeRawMsg) DDC_REQUIRE(aceMtiGetMetrics)}
#undef DDC_REQUIRE
  }
  std::string error(S16BIT result,const char *operation) {
    char buffer[256]{};aceErrorStrFn(result,buffer,sizeof(buffer));
    return std::string(operation)+": "+buffer+" ("+std::to_string(result)+")";
  }
};
class DdcSource final : public CaptureSource {
  std::unique_ptr<Runtime> runtime;
  CaptureConfig config;
  std::string closeFailure;
  CaptureFailure failure;
  bool draining{};unsigned drainAttempts{};std::string stopFailure;
  std::array<bool,2> opened{},started{},legacy{};
  std::array<U32BIT,2> identityFamily{};
  std::array<std::uint64_t,2> legacyPrevious{},legacyEpoch{},legacyLoss{};
  std::array<std::uint64_t,2> metricLoss{},metricOverflow{},overflowReads{};
  std::array<bool,2> legacySeen{};
  std::array<std::vector<std::uint64_t>,2> buffers;
  std::uint64_t sequence{},lost{},overflows{};
  std::array<RelativeClock48,2> clocks;
  std::chrono::steady_clock::time_point checked;
  std::string sdkError(S16BIT code,const char *operation,unsigned channel,const char *stage="receive") {
    failure={};failure.code=code;failure.operation=operation;failure.channel=int(channel);failure.logicalDevice=config.logicalDevices[channel];failure.stage=stage;
    failure.detail=runtime->error(code,operation);
    if(code==ACE_ERR_REG_ACCESS){failure.category=CaptureFailureCategory::Registry;failure.detail+=". Verify the Card Manager logical device assignment and registry access.";failure.recovery={"Open Card Manager and verify this logical device assignment","Rescan DDC and collect diagnostics"};}
    else if(code==ACE_ERR_DRIVER_OPEN){failure.category=CaptureFailureCategory::DriverOpen;failure.detail+=". Check Windows device status, driver binding, Card Manager assignments, permissions and other applications holding this channel.";failure.recovery={"Verify the started DDC driver and Card Manager assignment","Release the channel in RTPro or another application","Collect diagnostics before changing drivers"};}
    else if(code==ACE_ERR_INITIALIZED||code==ACE_ERR_OBJECT_INUSE){failure.category=CaptureFailureCategory::Ownership;failure.recovery={"Close the other application using this channel and reconnect"};}
    else if(code==ACE_ERR_NOT_SUPPORTED||code==ACE_ERR_INVALID_MODE||code==ACE_ERR_INVALID_MODE_OP){failure.category=CaptureFailureCategory::UnsupportedMode;failure.recovery={"Select a supported MTI or classic MT receive mode after disconnecting","Try a verified legacy x86 helper pack"};}
    else {failure.category=std::string(stage)=="configure"?CaptureFailureCategory::Configuration:CaptureFailureCategory::Integrity;failure.recovery={"Review the failed SDK operation and collect diagnostics"};}
    return failure.detail;
  }
  std::string integrityError(const char *detail,unsigned channel) {
    failure={};failure.category=CaptureFailureCategory::Integrity;failure.stage="receive";failure.operation="packet_validation";failure.channel=int(channel);failure.logicalDevice=config.logicalDevices[channel];failure.detail=detail;failure.recovery={"Preserve the incomplete recording and collect diagnostics"};return failure.detail;
  }
  std::string snapshotMetrics(SourceBatch &batch,bool final=false) {
    for(unsigned channel=0;channel<2;++channel)if(opened[channel]&&!legacy[channel]) {
      ACEX_MTI_METRICS metrics{};
      const auto result=runtime->aceMtiGetMetricsFn(config.logicalDevices[channel],&metrics);
      if(config.diagnostic && (final || result || metrics.u32MtiDroppedMsgs || metrics.u32MtiStkOverflowCount))
        config.diagnostic(final?"final_integrity_snapshot":"integrity_snapshot",result,
          "channel="+std::to_string(channel+1)+"; logical_device="+std::to_string(config.logicalDevices[channel])+"; dropped_raw="+std::to_string(metrics.u32MtiDroppedMsgs)+"; overflow_raw="+std::to_string(metrics.u32MtiStkOverflowCount)+"; fullness="+std::to_string(metrics.u32MtiStkPercentFull)+"; counter_reset_semantics=unverified");
      if(result)return sdkError(result,"DDC integrity metrics",channel);
      metricLoss[channel]=std::max(metricLoss[channel],std::uint64_t(metrics.u32MtiDroppedMsgs));
      metricOverflow[channel]=std::max(metricOverflow[channel],std::uint64_t(metrics.u32MtiStkOverflowCount));
    }
    lost=overflows=0;
    for(unsigned c=0;c<2;++c) {
      batch.channelLost[c]=legacy[c]?legacyLoss[c]:metricLoss[c];
      batch.channelOverflows[c]=std::max(metricOverflow[c],overflowReads[c]);
      lost+=batch.channelLost[c];overflows+=batch.channelOverflows[c];
    }
    batch.hardwareLost=lost;batch.hardwareOverflows=overflows;return {};
  }
public:
  ~DdcSource() override { close(); }
  std::string open(const CaptureConfig &value) override {
    close();if(!closeFailure.empty())return "Previous SDK cleanup failed: "+closeFailure;
    failure={};config=value;draining=false;drainAttempts=0;stopFailure.clear();sequence=lost=overflows=0;clocks={};legacy={};legacyPrevious={};legacyEpoch={};legacySeen={};legacyLoss={};metricLoss={};metricOverflow={};overflowReads={};
    try {
      runtime=std::make_unique<Runtime>(config);
      if(config.diagnostic) config.diagnostic("runtime_loaded",0,pathText(runtime->path)+"; API="+std::to_string(runtime->aceGetLibVersionFn())+
        "; compiled_MSGSTRUCT_bytes="+std::to_string(sizeof(MSGSTRUCT))+"; compiled_HWVERSIONINFO_bytes="+std::to_string(sizeof(HWVERSIONINFO))+
        "; pointer_bits="+std::to_string(sizeof(void *)*8)+"; export_detection_is_not_ABI_qualification");
      if(config.logicalDevices[0]==config.logicalDevices[1] && (config.enabled[0]||config.enabled[1]) && (config.enabled[2]||config.enabled[3]))
        throw std::runtime_error("Select distinct DDC logical devices for the two channels");
      std::string failures;
      for(unsigned channel=0;channel<2;++channel) {
        if(!config.enabled[channel*2] && !config.enabled[channel*2+1]) continue;
        if((config.stopToken.stop_requested() || (config.cancelRequested && config.cancelRequested()))) throw std::runtime_error("Connection cancelled");
        try {
        const auto device=config.logicalDevices[channel];
        std::uint32_t supplementary=0;
        const auto check=[&](S16BIT result,const char *operation) {
          if(config.diagnostic) config.diagnostic(operation,result,"logical_device="+std::to_string(device)+"; channel="+std::to_string(channel+1));
          if(result) {
            auto message=sdkError(result,operation,channel,"configure");
            if(std::string(operation).find("identity open")!=std::string::npos){failure.windowsCode=supplementary;failure.supplementaryWindows=true;}
            if(result==ACE_ERR_REG_ACCESS)message+=". Card Manager's logical assignment could not be read. Verify the selected logical device number and registry access; Windows device detection alone is insufficient.";
            if(result==ACE_ERR_DRIVER_OPEN) message+=". Logical device="+std::to_string(device)+"; runtime="+pathText(runtime->path)+"; API="+std::to_string(runtime->aceGetLibVersionFn())+". Runtime loaded, but the driver could not be opened. Check Windows device status, the matching DDC driver, Card Manager logical assignment, access permissions and other applications holding this channel. These are possible causes, not a confirmed diagnosis. Use Rescan DDC for Windows/driver/hash evidence.";
            throw std::runtime_error(message);
          }
        };
        if(device<0 || device>=MAX_NUM_OF_DEVICES) throw std::runtime_error("DDC logical device must be 0–31");
        SetLastError(ERROR_SUCCESS);
        auto result=runtime->aceInitializeFn(device,ACE_ACCESS_CARD,ACE_MODE_MT|ACE_NO_TT_RESET,0,0,0);
        const auto driverOpenWinError=GetLastError();
        supplementary=driverOpenWinError;
        if(config.diagnostic)config.diagnostic("driver_open_windows_context",int(driverOpenWinError),"SDK result="+std::to_string(result)+"; GetLastError is supplementary and may be stale if the SDK did not set it");
        check(result,"aceInitialize / DDC passive MT identity open");
        opened[channel]=true;
        HWVERSIONINFO identity{};
        check(runtime->aceGetHwVersionInfoFn(device,&identity),"aceGetHwVersionInfo");
        const auto modelLength=std::find(identity.szModelName,identity.szModelName+sizeof(identity.szModelName),0)-identity.szModelName;
        const std::string model(reinterpret_cast<const char *>(identity.szModelName),modelLength);
        if(config.diagnostic) config.diagnostic("hardware_identity",0,"logical_device="+std::to_string(device)+"; model="+model+"; driver="+std::to_string(identity.dwDriverVersion)+"; capabilities="+std::to_string(identity.dwCapabilities)+"; serial="+std::to_string(identity.dwSerialNumber)+"; firmware_raw="+std::to_string(identity.dwFwVersion)+"; hdl_raw="+std::to_string(identity.dwHdlVersion)+"; family="+std::to_string(identity.dwFamilyNumber)+"; firmware_compatibility=verify_vendor_family_encoding_and_release_notes");
        identityFamily[channel]=identity.dwFamilyNumber;
        if(config.receiveMode==DdcReceiveMode::MTI&&!(identity.dwCapabilities&HWVER_CAPABILITY_MTI)) {
          sdkError(ACE_ERR_NOT_SUPPORTED,"Requested MTI mode is not supported by this channel",channel,"configure");throw std::runtime_error(failure.detail);
        }
        legacy[channel]=config.receiveMode==DdcReceiveMode::LegacyMT || (config.receiveMode==DdcReceiveMode::Auto&&!(identity.dwCapabilities&HWVER_CAPABILITY_MTI));
        try{runtime->requireProtocol(legacy[channel]);}catch(const std::exception &e){sdkError(ACE_ERR_NOT_SUPPORTED,"Receive interface unavailable",channel,"configure");failure.detail=e.what();throw;}
        if(!legacy[channel]) {
          check(runtime->aceFreeFn(device),"aceFree / switch to MTI");opened[channel]=false;
          check(runtime->aceInitializeFn(device,ACE_ACCESS_CARD,ACE_MODE_MTI|ACE_NO_TT_RESET,0,0,0),"aceInitialize / DDC MTI initialize");opened[channel]=true;
        }
        if(config.diagnostic) config.diagnostic("monitor_protocol",0,"channel="+std::to_string(channel+1)+(legacy[channel]?"; protocol=MT_RAW40; encoding_tick_ns=2000; legacy timestamp extension cannot prove elapsed time through silent rollovers":"; protocol=MTI_CH10; encoding_tick_ns=2000; timebase=DDC_TMATS_TTRES_0; measurement_accuracy=unverified"));
        // 2 us is the SDK's supported native timebase (configop.h, mtiop.h TTRES:0).
        // DDC Chapter 10 timestamps
        // use the selected DDC TTRES native timebase; raw ticks and packets are retained.
        result=runtime->aceSetTimeTagResFn(device,ACE_TT_2US);
        check(result,"aceSetTimeTagRes / 2 us native timebase");
        U32BIT options=0;
        if(!config.enabled[channel*2]) options|=ACE_MTI_OPT_DISABLE_BUS_A;
        if(!config.enabled[channel*2+1]) options|=ACE_MTI_OPT_DISABLE_BUS_B;
        if(legacy[channel]) {
          // E2MA/EMACE use the classic MT stack. Bus selection is applied after reading;
          // the vendor sample restricts disable-bus options to ACEX devices.
          check(runtime->aceMTConfigureFn(device,ACE_MT_DOUBLESTK,ACE_MT_CMDSTK_4K,ACE_MT_DATASTK_32K,0),"aceMTConfigure / legacy passive monitor");
          check(runtime->aceMTInstallHBufFn(device,1024*40),"aceMTInstallHBuf / raw 40-word records");
          if(config.deviceState)config.deviceState(channel,DevicePhase::Ready,"Legacy MT configured; 2 us device ticks, silent rollover ambiguity is preserved");
          continue;
        }
        constexpr U32BIT blockBytes=65536;
        result=runtime->aceMTIConfigureFn(device,MTI_DEVBUF_SIZE_128K,32,blockBytes,FALSE,
          blockBytes/2-128,256,100,MTI_TIME_INT|MTI_NUM_WORDS|MTI_NUM_MSGS|MTI_OVERFLOW_INT,
          channel+1,0,0,0,options);
        check(result,"aceMTIConfigure / relative Chapter 10 time");
        buffers[channel].resize(blockBytes/sizeof(std::uint64_t));
        if(config.deviceState) config.deviceState(channel,DevicePhase::Ready,"Selected bus paths configured");
        } catch(const std::exception &error) {
          const auto message="Device "+std::to_string(config.logicalDevices[channel])+" / Channel "+std::to_string(channel+1)+": "+error.what();
          if(config.deviceState) config.deviceState(channel,DevicePhase::Failed,message);
          if(!failures.empty()) failures+='\n';failures+=message;
        }
      }
      if(!failures.empty()) throw std::runtime_error(failures);
      if((config.stopToken.stop_requested() || (config.cancelRequested && config.cancelRequested()))) throw std::runtime_error("Connection cancelled");
      checked=std::chrono::steady_clock::now();return {};
    } catch(const std::exception &error) { const std::string message=error.what();if(!failure){failure.category=runtime?CaptureFailureCategory::Configuration:CaptureFailureCategory::RuntimeSelection;failure.stage="open";failure.operation="open";failure.detail=message;failure.recovery={"Review selected runtime/provider identity and Card Manager assignments","Collect diagnostics"};}close();return message; }
  }
  void updateCallbacks(const CaptureConfig &value) override {
    config.diagnostic=value.diagnostic;config.deviceState=value.deviceState;config.stopToken=value.stopToken;
  }
  std::string startMonitoring() override {
    draining=false;drainAttempts=0;stopFailure.clear();
    sequence=lost=overflows=0;clocks={};legacyPrevious={};legacyEpoch={};legacySeen={};legacyLoss={};metricLoss={};metricOverflow={};overflowReads={};
    try {
      // Every selected channel has been configured before any producer starts.
      for(unsigned channel=0;channel<2;++channel) if(opened[channel]) {
        if((config.stopToken.stop_requested() || (config.cancelRequested && config.cancelRequested())))return "Recording start cancelled";
        const auto result=(legacy[channel]?runtime->aceMTStartFn(config.logicalDevices[channel]):runtime->aceMTIStartFn(config.logicalDevices[channel]));
        if(config.diagnostic)config.diagnostic(legacy[channel]?"aceMTStart":"aceMTIStart",result,"channel="+std::to_string(channel+1));
        if(result)return sdkError(result,legacy[channel]?"aceMTStart":"aceMTIStart",channel,"start");
        started[channel]=true;
      }
      checked=std::chrono::steady_clock::now();return {};
    }catch(const std::exception &e){return e.what();}
  }
  std::string poll(SourceBatch &batch,std::stop_token stop) override {
    const auto pollBegan=std::chrono::steady_clock::now();
    struct Measure{SourceBatch &b;std::chrono::steady_clock::time_point start;~Measure(){const auto total=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();b.packetProcessingNs=total>b.pollNs?total-b.pollNs:0;}} measure{batch,pollBegan};
    bool any=false;
    for(unsigned channel=0;channel<2 && !stop.stop_requested();++channel) {
      if(!opened[channel]) continue;
      if(legacy[channel]) {
        std::array<U16BIT,40*256> raw{};U32BIT count=0,stackLost=0,hostLost=0;
        const auto device=config.logicalDevices[channel];
        const auto callBegan=std::chrono::steady_clock::now();
        const auto result=runtime->aceMTGetHBufMsgsRawFn(device,raw.data(),raw.size(),&count,&stackLost,&hostLost);
        batch.pollNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-callBegan).count();
        if(result)return sdkError(result,"Legacy MT read (SDK operation failed; check Windows device presence)",channel);
        legacyLoss[channel]+=std::uint64_t(stackLost)+hostLost;lost=legacyLoss[0]+legacyLoss[1];
        if(count>256)return integrityError("Legacy MT returned too many raw messages",channel);
        if(count){any=true;const auto bytes=reinterpret_cast<const std::uint8_t *>(raw.data());batch.packets.push_back({channel+1,{bytes,bytes+count*80},false});}
        for(unsigned i=0;i<count;++i) {
          MSGSTRUCT decoded{};const auto status=runtime->aceMTDecodeRawMsgFn(device,raw.data()+i*40,&decoded);
          if(status)return sdkError(status,"Legacy MT raw decode",channel);
          if(decoded.wWordCount>32)return integrityError("Legacy MT decoded invalid word count",channel);
          const std::uint64_t ticks=std::uint64_t(decoded.wTimeTag)|(std::uint64_t(decoded.wTimeTag2)<<16)|(std::uint64_t(decoded.wTimeTag3)<<32);
          // E2MA may expose only a 16-bit tag. Extend observable backwards wraps,
          // flag all classic timestamps as ambiguous across silent rollover intervals.
          const bool shortTag=identityFamily[channel]==E2MACE || identityFamily[channel]==EMACE;
          if(shortTag&&legacySeen[channel]&&ticks<legacyPrevious[channel])legacyEpoch[channel]+=65536;
          legacyPrevious[channel]=ticks;legacySeen[channel]=true;
          Record record;record.id=++sequence;record.source=2;record.channel=channel+1;record.bus=bool(decoded.wBlkSts&ACE_MT_BSW_CHNL);
          record.commandWord=decoded.wCmdWrd1;record.commandWord2=decoded.wCmdWrd2;record.statusWord=decoded.wStsWrd1;record.statusWord2=decoded.wStsWrd2;
          record.vendorStatus=decoded.wBlkSts;record.flags=decoded.wBlkSts&ACE_MT_BSW_ERROR;
          record.validity=(decoded.wCmdWrd1Flg?1:0)|(decoded.wCmdWrd2Flg?2:0)|(decoded.wStsWrd1Flg?4:0)|(decoded.wStsWrd2Flg?8:0);
          if(shortTag)record.validity|=0x100; // Legacy timestamp rollover ambiguity, not fabricated precision.
          if(!decoded.wCmdWrd1Flg)record.flags|=0x8000;
          record.messageType=decoded.wType;record.deviceTicks=ticks;record.tickResolutionNs=2000;record.timestampNs=(ticks+legacyEpoch[channel])*2000;
          record.hostMonotonicNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
          record.words.assign(decoded.aDataWrds,decoded.aDataWrds+decoded.wWordCount);
          if(config.enabled[channel*2+record.bus])batch.records.push_back(std::move(record));
        }
        continue;
      }
      auto supplied=reinterpret_cast<PMTI_CH10_DATA_PKT>(buffers[channel].data());
      auto packet=supplied;
      const auto device=config.logicalDevices[channel];
      const auto callBegan=std::chrono::steady_clock::now();
      const auto result=runtime->aceMTIGetCh10DataPktFn(device,&packet,0);
      batch.pollNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-callBegan).count();
      if(result==ACE_ERR_DATA_UNAVAILABLE || result==ACE_ERR_TIMEOUT ||
         result==ACE_ERR_BUFFER_UNAVAILABLE || result==ACE_ERR_BUFFER_BUSY) continue;
      if(result==ACE_ERR_BUFFER_OVERFLOW) { ++overflowReads[channel];continue; }
      if(result) return sdkError(result,"DDC packet read (SDK operation failed; check Windows device presence)",channel);
      any=true;
      struct Release {
        Runtime *runtime;S16BIT device;PMTI_CH10_DATA_PKT *packet;
        bool released{};
        ~Release(){if(!released)runtime->aceMTIFreeCh10DataPktFn(device,packet);}
      } release{runtime.get(),S16BIT(device),&packet};
      if(!packet || packet->u16PktSyncPattern!=IRIG_CH10_PKT_SYNC_PATT ||
         !validChapter10Envelope({reinterpret_cast<const std::uint8_t *>(packet),65536}))
        return integrityError("DDC returned an invalid Chapter 10 packet",channel);
      const auto bytes=reinterpret_cast<const std::uint8_t *>(packet);
      batch.packets.push_back({channel+1,{bytes,bytes+packet->u32PktLength}});
      if(bytes[14]&0xc0) return integrityError("Unsupported DDC secondary/absolute time format; raw packet preserved, timestamp not fabricated",channel);
      auto current=reinterpret_cast<U16BIT *>(reinterpret_cast<std::uint8_t *>(packet)+28);
      const auto end=bytes+24+packet->u32DataLength;
      const auto count=packet->u32ChnlSpecificData&IRIG_CH10_PKT_MSG_COUNT_MASK;
      if(count>4096) return integrityError("DDC packet message count exceeds bounded packet capacity",channel);
      for(unsigned message=0;message<count;++message) {
        const auto raw=reinterpret_cast<const std::uint8_t *>(current);
        if(raw<bytes+28 || raw+14>end) return integrityError("Truncated DDC message header",channel);
        std::uint64_t ticks=0;std::memcpy(&ticks,raw,sizeof(ticks));
        // RTC uses 48 bits; high 16 bits in the intra-packet field are unused.
        ticks&=RelativeClock48::mask;const auto previousWraps=clocks[channel].wraps;
        const auto extended=clocks[channel].extend(ticks);
        if(clocks[channel].wraps!=previousWraps && config.diagnostic) config.diagnostic("rtc_rollover",0,"channel="+std::to_string(channel+1));
        MSGSTRUCT decoded{};
        const auto status=runtime->aceMTIDecodeRawMsgFn(packet,&current,&decoded);
        if(status!=ACE_ERR_SUCCESS && !(status==ACE_ERR_MTI_EOB && message+1==count))
          return sdkError(status,"DDC message decode",channel);
        if(!validDecodedMessageBounds(std::uintptr_t(raw)-std::uintptr_t(bytes),std::uintptr_t(current)-std::uintptr_t(bytes),std::uintptr_t(end)-std::uintptr_t(bytes),decoded.wWordCount)) return integrityError("DDC decoded invalid message bounds",channel);
        Record record;
        record.id=++sequence;record.source=1;record.channel=channel+1;
        record.bus=bool(decoded.wBlkSts&ACE_MT_BSW_CHNL);
        record.commandWord=decoded.wCmdWrd1;record.commandWord2=decoded.wCmdWrd2;
        record.statusWord=decoded.wStsWrd1;record.statusWord2=decoded.wStsWrd2;
        record.vendorStatus=decoded.wBlkSts;record.flags=decoded.wBlkSts&ACE_MT_BSW_ERROR;
        record.validity=(decoded.wCmdWrd1Flg?1:0)|(decoded.wCmdWrd2Flg?2:0)|(decoded.wStsWrd1Flg?4:0)|(decoded.wStsWrd2Flg?8:0);
        if(!decoded.wCmdWrd1Flg) record.flags|=0x8000;
        record.messageType=decoded.wType;record.deviceTicks=ticks;record.tickResolutionNs=2000;
        if(extended>UINT64_MAX/2000) return integrityError("DDC timestamp overflow",channel);
        record.timestampNs=extended*2000;
        record.hostMonotonicNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        record.words.assign(decoded.aDataWrds,decoded.aDataWrds+decoded.wWordCount);
        if(config.enabled[channel*2+record.bus]) batch.records.push_back(std::move(record));
      }
      release.released=true;
      const auto freed=runtime->aceMTIFreeCh10DataPktFn(device,&packet);
      // SDK 3.6.15 mtiedemo.c calls this a "NOP in Windows"; mtidemo.c
      // explicitly ignores its error. The USB implementation returns -68 for
      // the caller-provided buffer. Never apply this exception to SDK-owned data.
      if(freed && !(freed==ACE_ERR_NOT_SUPPORTED && packet==supplied))
        return sdkError(freed,"DDC packet release (SDK operation failed; device removal not established)",channel);
    }
    const auto now=std::chrono::steady_clock::now();
    if(now-checked>=std::chrono::milliseconds(100)) {
      if(auto error=snapshotMetrics(batch);!error.empty())return error;
      checked=now;
    }
    for(unsigned c=0;c<2;++c){batch.channelLost[c]=legacy[c]?legacyLoss[c]:metricLoss[c];batch.channelOverflows[c]=std::max(metricOverflow[c],overflowReads[c]);}
    batch.hardwareLost=batch.channelLost[0]+batch.channelLost[1];batch.hardwareOverflows=batch.channelOverflows[0]+batch.channelOverflows[1];
    if(!any) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {};
  }
  void close() override {
    const bool hadRuntime=bool(runtime);
    if(runtime) for(unsigned channel=0;channel<2;++channel) {
      if(started[channel]) {const auto result=(legacy[channel]?runtime->aceMTStopFn(config.logicalDevices[channel]):runtime->aceMTIStopFn(config.logicalDevices[channel]));if(config.diagnostic) config.diagnostic(legacy[channel]?"aceMTStop":"aceMTIStop",result,"channel="+std::to_string(channel+1));if(result)closeFailure+=runtime->error(result,"DDC close stop")+"; ";}
      if(opened[channel]) {const auto result=runtime->aceFreeFn(config.logicalDevices[channel]);if(config.diagnostic) config.diagnostic("aceFree",result,"channel="+std::to_string(channel+1));if(result)closeFailure+=runtime->error(result,"aceFree")+"; ";}
      started[channel]=opened[channel]=false;
    }
    runtime.reset();
    if(hadRuntime&&!closeFailure.empty()){if(!failure){failure.category=CaptureFailureCategory::Cleanup;failure.stage="close";failure.operation="close";failure.detail=closeFailure;}else failure.detail+="; cleanup: "+closeFailure;failure.cleanupFailed=true;}
  }
  std::string closeError() const override {return closeFailure;}
  CaptureFailure lastFailure() const override {return failure;}
  std::string stopMonitoring() override {
    if(draining)return stopFailure;
    draining=true;drainAttempts=0;stopFailure.clear();
    for(unsigned channel=0;channel<2;++channel) if(started[channel]) {
      const auto result=(legacy[channel]?runtime->aceMTStopFn(config.logicalDevices[channel]):runtime->aceMTIStopFn(config.logicalDevices[channel]));
      if(config.diagnostic) config.diagnostic(legacy[channel]?"aceMTStop":"aceMTIStop",result,"channel="+std::to_string(channel+1));
      if(result){if(!stopFailure.empty())stopFailure+="; ";stopFailure+=sdkError(result,"DDC monitor stop",channel,"stop");}
      else started[channel]=false;
    }
    return stopFailure;
  }
  std::string drainNext(SourceBatch &batch,bool &more) override {
    if(!draining)stopMonitoring();
    more=false;if(!runtime)return stopFailure;
    if(drainAttempts++>=256){failure={};failure.category=CaptureFailureCategory::Integrity;failure.stage="drain";failure.operation="drainNext";failure.detail="DDC stop drain exceeded bounded buffer pool";return failure.detail;}
    const auto message=poll(batch,{});more=!batch.packets.empty()&&message.empty();
    if(!message.empty()){snapshotMetrics(batch,true);return message;}
    if(!more){const auto metricsError=snapshotMetrics(batch,true);return !stopFailure.empty()?stopFailure:metricsError;}
    return {};
  }
  std::string finish(SourceBatch &tail) override {
    stopMonitoring();
    for(bool more=true;more;) {
      SourceBatch batch;const auto message=drainNext(batch,more);
      if(tail.records.size()+batch.records.size()>262144||tail.packets.size()+batch.packets.size()>512)return "DDC legacy drain adapter exceeded batch bounds; use incremental drain";
      tail.records.insert(tail.records.end(),std::make_move_iterator(batch.records.begin()),std::make_move_iterator(batch.records.end()));
      tail.packets.insert(tail.packets.end(),std::make_move_iterator(batch.packets.begin()),std::make_move_iterator(batch.packets.end()));
      tail.hardwareLost=batch.hardwareLost;tail.hardwareOverflows=batch.hardwareOverflows;tail.channelLost=batch.channelLost;tail.channelOverflows=batch.channelOverflows;
      if(!message.empty())return message;
    }
    return {};
  }
};
}
DdcRuntimeInfo inspectVendorDdcRuntime(const std::filesystem::path &path) {
  try { CaptureConfig c;c.runtime=path;c.runtimeSha256=ddcRuntimeSha256(path);c.runtimeArchitecture=sizeof(void *)==4?"x86":"x64";Runtime runtime(c);const auto version=runtime.aceGetLibVersionFn();
    return {true,"DDC runtime loaded; API "+std::to_string(version>>8)+"."+std::to_string((version>>4)&15)+"."+std::to_string(version&15)+". Connect hardware and select SDK logical channels.",runtime.path};
  } catch(const std::exception &error) {return {false,error.what(),path};}
}
std::unique_ptr<CaptureSource> makeVendorDdcSource(){return std::make_unique<DdcSource>();}
#else
namespace {
class UnavailableDdc final : public CaptureSource {
public:
  std::string open(const CaptureConfig &) override {return "DDC SDK adapter was not enabled in this build";}
  std::string poll(SourceBatch &,std::stop_token) override {return "DDC unavailable";}
  void close() override {}
};
}
std::unique_ptr<CaptureSource> makeVendorDdcSource(){return std::make_unique<UnavailableDdc>();}
DdcRuntimeInfo inspectVendorDdcRuntime(const std::filesystem::path &path) {return {false,"Build with an installed DDC SDK to enable hardware monitoring",path};}
#endif
}
