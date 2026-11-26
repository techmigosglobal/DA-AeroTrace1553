#include "example_simulation.hpp"
#include "acquisition.hpp"
#include "capture_queue.hpp"
#include "capture_file.hpp"
#include <algorithm>
#include <chrono>
#include <future>
#include <stdexcept>
#include <iomanip>
#include <locale>

namespace aero {
namespace {
std::string pathUtf8(const std::filesystem::path &path){const auto text=path.generic_u8string();return {text.begin(),text.end()};}
std::string identityJson(const CaptureIdentity &value){
  return "{\"backend\":"+jsonString(value.backend)+",\"receive_mode\":"+jsonString(value.receiveMode)+",\"runtime\":"+jsonString(pathUtf8(value.runtime))+
    ",\"runtime_sha256\":"+jsonString(value.runtimeSha256)+",\"provider\":"+jsonString(pathUtf8(value.provider))+",\"provider_id\":"+jsonString(value.providerId)+
    ",\"sdk_identity\":"+jsonString(value.sdkIdentity)+",\"header_sha256\":"+jsonString(value.headerSha256)+",\"helper\":"+jsonString(pathUtf8(value.helper))+
    ",\"helper_sha256\":"+jsonString(value.helperSha256)+",\"manifest\":"+jsonString(pathUtf8(value.manifest))+",\"qualification\":"+jsonString(value.qualification)+"}";
}
}
Acquisition::~Acquisition() { stop(); }
bool Acquisition::start(const std::filesystem::path &path,std::array<bool,4> enabled) {
  CaptureConfig config;config.hardware=false;config.enabled=enabled;
  return startConfigured(path,config);
}
bool Acquisition::startConfigured(const std::filesystem::path &path,CaptureConfig config,std::unique_ptr<CaptureSource> source) {
  auto ready=beginConfigured(path,std::move(config),std::move(source));
  const auto ok=ready.get();if(!ok && worker.joinable()) worker.join();
  return ok;
}
bool Acquisition::connectConfigured(CaptureConfig config,std::unique_ptr<CaptureSource> source) {
  auto ready=beginConnection(std::move(config),std::move(source));const bool ok=ready.get();
  if(worker.joinable())worker.join();return ok;
}
std::future<bool> Acquisition::beginConnection(CaptureConfig config,std::unique_ptr<CaptureSource> source) {
  stop();
  if(cleanupFault){std::promise<bool> rejected;auto ready=rejected.get_future();rejected.set_value(false);return ready;}
  {std::lock_guard lock(mutex);state={};pending.clear();live.reset();rtLive.reset();state.hardware=config.hardware;}
  std::promise<bool> completion;auto ready=completion.get_future();
  if(!source)source=config.hardware?makeDdcSource():makeSimulator();
  if(config.hardware)source=makeSdkOwnedSource(std::move(source));
  worker=std::jthread([this,config,source=std::move(source),completion=std::move(completion)](std::stop_token token) mutable {
    config.stopToken=token;
    const auto userState=config.deviceState;
    config.deviceState=[this,userState](unsigned channel,DevicePhase phase,const std::string &message) {
      if(channel<2){std::lock_guard lock(mutex);state.devices[channel]={phase,message};if(phase==DevicePhase::Ready)state.configuredChannels[channel]=true;}
      if(userState)userState(channel,phase,message);
    };
    std::string message;
    try {
      if(std::none_of(config.enabled.begin(),config.enabled.end(),[](bool on){return on;}))throw std::runtime_error("Enable at least one channel/bus");
      for(unsigned c=0;c<2;++c)config.deviceState(c,config.enabled[c*2]||config.enabled[c*2+1]?DevicePhase::Checking:DevicePhase::Disabled,{});
      message=source->open(config);
      if(message.empty()&&token.stop_requested())message="Connection cancelled";
    }catch(const std::exception &e){message=e.what();}
    {const auto identity=source->identity();std::lock_guard lock(mutex);state.identity=identity;}
    if(!message.empty()) {
      const auto failure=source->lastFailure();
      source->close();
      const auto closeFailure=source->closeError();
      if(!closeFailure.empty())cleanupFault=true;
      std::lock_guard lock(mutex);state.failed=true;state.error=message;state.failure=failure;
      state.cleanupBlocked=cleanupFault;
      if(!closeFailure.empty())state.error+="; cleanup failed: "+closeFailure;
      for(unsigned c=0;c<2;++c)if(config.enabled[c*2]||config.enabled[c*2+1]) {
        auto &device=state.devices[c];
        if(device.phase==DevicePhase::Ready)device={DevicePhase::Stopped,"Configuration rolled back because another channel or prerequisite failed"};
        else if(device.phase!=DevicePhase::Failed)device={DevicePhase::Stopped,"Not checked — "+message};
      }
      completion.set_value(false);return;
    }
    for(unsigned c=0;c<2;++c)if(config.enabled[c*2]||config.enabled[c*2+1])config.deviceState(c,DevicePhase::Ready,"Connected; acquisition waits for Start recording");
    source->updateCallbacks(CaptureConfig{});
    retainedConfig=config;retainedConfig.deviceState={};retainedConfig.diagnostic={};retainedConfig.stopToken={};
    retainedSource=std::move(source);connectedFlag=true;completion.set_value(true);
  });
  return ready;
}
std::future<bool> Acquisition::beginConfigured(const std::filesystem::path &path,CaptureConfig config,std::unique_ptr<CaptureSource> source) {
  stopRecording();
  const bool reuse=!source && retainedSource && config.hardware && retainedConfig.hardware==config.hardware &&
    retainedConfig.enabled==config.enabled && retainedConfig.logicalDevices==config.logicalDevices && retainedConfig.runtime==config.runtime &&
    retainedConfig.backend==config.backend && retainedConfig.receiveMode==config.receiveMode && retainedConfig.connectionPolicy==config.connectionPolicy && retainedConfig.helperPack==config.helperPack;
  if(reuse)source=std::move(retainedSource);
  else {if(retainedSource)stop();connectedFlag=false;}
  if(cleanupFault){std::promise<bool> rejected;auto ready=rejected.get_future();rejected.set_value(false);return ready;}
  retainConnection=true;
  {
    std::lock_guard lock(mutex);state={};pending.clear();live.reset();rtLive.reset();state.hardware=config.hardware;
  }
  if(config.writerCapacityBytes<1 || config.writerCapacityBytes>64*1024*1024 ||
     config.displayCapacity<1 || config.displayCapacity>65536 ||
     std::none_of(config.enabled.begin(),config.enabled.end(),[](bool on){return on;})) {
    {std::lock_guard lock(mutex);state.failed=true;state.error="Enable at least one stream and use a display capacity of 1–65536";}
    std::promise<bool> rejected;auto ready=rejected.get_future();rejected.set_value(false);return ready;
  }
  if(!source) source=config.hardware ? makeDdcSource() : makeSimulator();
  if(config.hardware)source=makeSdkOwnedSource(std::move(source));
  std::promise<bool> started;auto ready=started.get_future();
  worker=std::jthread([this,path,config,reuse,source=std::move(source),started=std::move(started)](std::stop_token stop) mutable {
    using namespace std::chrono;
    config.stopToken=stop;
    Recorder writer;
    std::error_code destinationError;
    if(std::filesystem::exists(path,destinationError)||destinationError) {
      source->close();
      connectedFlag=false;
      {
        std::lock_guard lock(mutex);
        state.failed=true;
        state.error="Cannot create a new recording at the selected path";
      }
      started.set_value(false);
      return;
    }
    const auto utf8=path.u8string();const std::string captureName(utf8.begin(),utf8.end());
    auto eventPath=path;eventPath+=".events.jsonl";std::ofstream events(eventPath,std::ios::trunc);
    std::mutex eventMutex;
    std::deque<std::string> journal;
    auto emit=[&](const std::string &event,const std::string &fields) {
      std::ostringstream entry;
      entry<<"{\"utc_epoch_ms\":"<<duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
        <<",\"host_steady_ns\":"<<duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count()
        <<",\"run_id\":"<<jsonString(config.runId)<<",\"capture\":"<<jsonString(captureName)<<",\"event\":"<<jsonString(event)<<','<<fields<<"}\n";
      auto line=std::move(entry).str();
      {
        std::lock_guard lock(eventMutex);
        if(journal.size()<512 && line.size()<=16384){journal.push_back(std::move(line));return;}
      }
      std::lock_guard lock(mutex);++state.diagnosticSkipped;
    };
    auto writeJournal=[&] {
      std::deque<std::string> batch;
      {std::lock_guard lock(eventMutex);batch.swap(journal);}
      for(const auto &line:batch)events<<line;
    };
    config.diagnostic=[&](const std::string &operation,int result,const std::string &detail) {
      emit("ddc_api","\"operation\":"+jsonString(operation)+",\"result\":"+std::to_string(result)+",\"detail\":"+jsonString(detail));
    };
    config.deviceState=[this,&emit](unsigned channel,DevicePhase phase,const std::string &message) {
      emit("device_state","\"channel\":"+std::to_string(channel+1)+",\"phase\":"+std::to_string(int(phase))+",\"detail\":"+jsonString(message));
      if(channel>=2) return;std::lock_guard lock(mutex);state.devices[channel]={phase,message};if(phase==DevicePhase::Ready)state.configuredChannels[channel]=true;
    };
    for(unsigned channel=0;channel<2;++channel)
      config.deviceState(channel,config.enabled[channel*2]||config.enabled[channel*2+1] ? DevicePhase::Checking : DevicePhase::Disabled,{});
    using PacketFile=std::unique_ptr<std::FILE,decltype(&std::fclose)>;
    std::array<PacketFile,4> packets{PacketFile(nullptr,std::fclose),PacketFile(nullptr,std::fclose),PacketFile(nullptr,std::fclose),PacketFile(nullptr,std::fclose)};
    LiveDecoder decoder;
    RtMonitorFrame rtMonitor;rtMonitor.session=captureName;
    std::uint64_t decoderRevision=UINT64_MAX;
    std::uint64_t decoderTraceRevision=UINT64_MAX;
    std::ofstream profileTimeline;
    std::vector<Signal> activeMappings;
    SignalChecks checks;
    std::array<std::uint64_t,4> lastCard{};
    std::array<steady_clock::time_point,4> lastHost{};
    auto checksPath=path;checksPath+=".checks.jsonl";std::ofstream checkLog(checksPath);
    auto publishChecks=[&] {
      auto batch=checks.drain();if(batch.empty()) return;
      for(const auto &e:batch) checkLog<<"{\"timestamp_ns\":\""<<e.timestampNs<<"\",\"record\":\""<<e.recordId<<"\",\"channel\":"<<e.channel<<",\"bus\":"<<e.bus<<",\"signal\":"<<jsonString(e.signalId)<<",\"kind\":"<<jsonString(e.kind)<<",\"detail\":"<<jsonString(e.detail)<<"}\n";
      checkLog.flush();if(!checkLog) throw std::runtime_error("Signal check log write failed");
      std::lock_guard lock(mutex);state.checkCount+=batch.size();
      state.checkEvents.insert(state.checkEvents.end(),batch.begin(),batch.end());
      if(state.checkEvents.size()>512) state.checkEvents.erase(state.checkEvents.begin(),state.checkEvents.end()-512);
    };
    auto fail=[this,&emit,&source](const std::string &message) { const auto failure=source?source->lastFailure():CaptureFailure{};emit("capture_error","\"detail\":"+jsonString(message));std::lock_guard lock(mutex);
      if(failure&&!state.failure)state.failure=failure;
      if(!state.failed)state.error=message;else if(state.error.find(message)==std::string::npos&&state.error.size()<8192)state.error+="; "+message;state.failed=true;
      for(auto &device:state.devices) {
        if(device.phase==DevicePhase::Ready) device={DevicePhase::Stopped,"Configuration closed: the other channel is not ready"};
        else if(device.phase==DevicePhase::Checking) device={DevicePhase::Stopped,"Not checked — "+message};
        else if(device.phase==DevicePhase::Monitoring) device={DevicePhase::Failed,message};
      }
    };
    CaptureQueue rawQueue(config.writerCapacityBytes),displayQueue(16*1024*1024);
    std::jthread diskWorker,displayWorker;
    std::string diskError;
    std::atomic<bool> diskFailed{};
    std::atomic<bool> displayFailed{};
    bool rawRejected{};
    auto writeBatch=[&](const CaptureQueue::Entry &entry) {
      const auto &batch=*entry.batch;
      const auto start=steady_clock::now();
      if(!writer.appendBatch(batch.records))throw std::runtime_error("Recording write failed");
      const auto packetStart=steady_clock::now();
      for(const auto &packet:batch.packets) {
        if(packet.channel<1 || packet.channel>2)throw std::runtime_error("Invalid source packet channel");
        const auto slot=(packet.channel-1)*2+!packet.chapter10;
        auto &output=packets[slot];
        if(!output) {
          auto raw=path;raw+=".ch"+std::to_string(packet.channel)+(packet.chapter10?".ch10":".mt40");
          output.reset(capture_file::open(raw,true));
        }
        if(!capture_file::write(output.get(),packet.bytes.data(),packet.bytes.size()))
          throw std::runtime_error("Native packet write failed");
      }
      const auto stamp=duration_cast<nanoseconds>(entry.received.time_since_epoch()).count();
      const auto written=steady_clock::now();
      std::lock_guard lock(mutex);
      state.recorded+=batch.records.size();
      state.timings[RecordingWrite].observe(duration_cast<nanoseconds>(packetStart-start).count());
      if(!batch.packets.empty())state.timings[PacketWrite].observe(duration_cast<nanoseconds>(written-packetStart).count());
      for(const auto &record:batch.records)if(record.channel>=1 && record.channel<=2 && record.bus<2) {
        const auto stream=(record.channel-1)*2+record.bus;
        ++state.sourceCounts[stream];
        state.lastTrafficNs[stream]=stamp;
        state.clocks[stream].observe(record.timestampNs);
      }
    };
    auto checkpoint=[&] {
      if(!writer.checkpoint())throw std::runtime_error("Recording durable checkpoint failed");
      for(auto &output:packets)if(output && !capture_file::checkpoint(output.get()))
        throw std::runtime_error("Native packet durable checkpoint failed");
      std::lock_guard lock(mutex);
      state.checkpointed=state.recorded;
    };
    auto runWriter=[&] {
      auto nextSync=steady_clock::now()+seconds(1);
      try {
        while(!rawQueue.finished()) {
          auto entry=rawQueue.take(milliseconds(25));
          if(entry) {
            writeBatch(entry);
            rawQueue.release(entry.bytes);
            const auto [queued,peak]=rawQueue.occupancy();
            std::lock_guard lock(mutex);
            state.writerQueueBytes=queued;state.writerQueueHighWater=std::max(state.writerQueueHighWater,peak);
          }
          const auto now=steady_clock::now();
          if(now>=nextSync) {
            checkpoint();
            writeJournal();
            std::error_code err;
            const auto free=std::filesystem::space(path.parent_path().empty()?std::filesystem::current_path():path.parent_path(),err);
            if(err || free.available<100*1024*1024)throw std::runtime_error("Recording stopped: low disk space");
            {std::lock_guard lock(mutex);state.timings[Flush].observe(duration_cast<nanoseconds>(steady_clock::now()-now).count());}
            nextSync=steady_clock::now()+seconds(1);
          }
        }
        checkpoint();
      } catch(const std::exception &error) {
        diskError=error.what();
        diskFailed=true;
        rawQueue.close();
      }
      if(!writer.close() && diskError.empty())diskError="Final recording close failed";
      for(auto &output:packets)if(output && std::fclose(output.release())!=0 && diskError.empty())
        diskError="Final native packet close failed";
    };
    auto enqueue=[&](SourceBatch &&batch) {
      if(batch.records.empty() && batch.packets.empty())return;
      const auto bytes=captureBatchBytes(batch);
      auto shared=std::make_shared<const SourceBatch>(std::move(batch));
      const auto received=steady_clock::now();
      const auto result=rawRejected?CaptureQueue::Result::Closed:rawQueue.push({shared,bytes,received});
      const auto [queued,peak]=rawQueue.occupancy();
      {
        std::lock_guard lock(mutex);
        state.writerQueueBytes=queued;state.writerQueueHighWater=std::max(state.writerQueueHighWater,peak);
        if(result!=CaptureQueue::Result::Accepted)state.writerRejected+=shared->records.size();
      }
      if(result!=CaptureQueue::Result::Accepted) {
        rawRejected=true;
        throw std::runtime_error(result==CaptureQueue::Result::Full?"Recording queue saturated; capture is incomplete":"Recording writer stopped");
      }
      if(displayQueue.push({shared,bytes,received})!=CaptureQueue::Result::Accepted) {
        std::lock_guard lock(mutex);
        state.analysisSkipped+=shared->records.size();
        state.displayDrops+=shared->records.size();
      }
    };
    auto runDisplay=[&] {
      auto published=steady_clock::now()-milliseconds(50);
      auto analysisRules=config.checkRules;
      bool revisionPending=true;
      bool commandCharts=false;
      const SourceBatch empty;
      try {
        while(!displayQueue.finished()) {
          auto entry=displayQueue.take(milliseconds(25));
          const auto &batch=entry ? *entry.batch : empty;
          std::vector<Signal> replacement;
          std::vector<unsigned> traces;bool changeTraces=false;
          CommandPlotSelection commandPlot;
          bool replace=false;
          {
            std::lock_guard lock(mutex);
            if(decoderRevision!=profileRevision) {replacement=requestedProfile;decoderRevision=profileRevision;replace=true;}
            if(replace || decoderTraceRevision!=traceRevision) {
              traces=requestedTraces;commandPlot=requestedCommandPlot;
              decoderTraceRevision=traceRevision;changeTraces=true;
            }
          }
          if(replace) {
            activeMappings=replacement;
            decoder.configure(std::move(replacement),decoderRevision,!config.hardware);
            revisionPending=true;
            if(!validateChecks(analysisRules,activeMappings).empty())analysisRules.clear();
            checks.configure(activeMappings,analysisRules,!config.hardware);
            lastHost={};
            emit("profile_activated","\"revision\":"+std::to_string(decoderRevision)+",\"signals\":"+std::to_string(activeMappings.size()));
          }
          if(changeTraces) {
            commandCharts=!commandPlot.command.empty();
            decoder.selectTraces(traces);
            decoder.selectCommandPlot(std::move(commandPlot));
          }

          std::array<std::uint64_t,TimingStageCount> durations{};
          auto timedAt=steady_clock::now();
          auto measured=[&](TimingStage stage){const auto end=steady_clock::now();durations[stage]+=duration_cast<nanoseconds>(end-timedAt).count();timedAt=end;};
          if(revisionPending && !batch.records.empty()) {
            const auto profile=serializeProfile(activeMappings);
            profileTimeline<<"{\"revision\":"<<decoderRevision<<",\"first_record\":"<<batch.records.front().id<<",\"profile\":"<<profile<<"}\n";
            profileTimeline.flush();
            auto profilePath=path;profilePath+=".profile.json";
            std::ofstream snapshot(profilePath,std::ios::trunc);snapshot<<profile;snapshot.flush();
            if(!profileTimeline || !snapshot) throw std::runtime_error("Cannot preserve ICD profile provenance");
            revisionPending=false;
          }
          decoder.consume(batch.records);measured(Conversion);
          for(const auto &r:batch.records)rtMonitor.consume(r);measured(RtAggregation);
          const auto now=entry ? entry.received : steady_clock::now();
          for(const auto &record:batch.records) {
            checks.consume(record);publishChecks();
            if(record.channel>=1 && record.channel<=2 && record.bus<2) {const auto k=(record.channel-1)*2+record.bus;lastCard[k]=record.timestampNs;lastHost[k]=now;}
          }
          // Continue missing-signal timers while a previously observed stream is silent.
          for(unsigned k=0;k<4;++k) if(lastHost[k]!=steady_clock::time_point{}) {
            checks.advance(k,lastCard[k]+duration_cast<nanoseconds>(now-lastHost[k]).count());publishChecks();
          }
          measured(SignalChecking);
          std::shared_ptr<const LiveFrame> snapshot;
          std::shared_ptr<const RtMonitorFrame> rtSnapshot;
          if(now-published>=milliseconds(commandCharts?100:50) || replace || changeTraces) {
            snapshot=std::make_shared<LiveFrame>(decoder.snapshot());rtSnapshot=std::make_shared<RtMonitorFrame>(rtMonitor);published=now;
          }
          measured(Publication);

          {
            std::lock_guard lock(mutex);
            state.decodedValues=decoder.conversions();state.invalidValues=decoder.invalidConversions();
            if(snapshot && snapshot->revision==profileRevision)live=std::move(snapshot);
            if(rtSnapshot)rtLive=std::move(rtSnapshot);
            for(const auto &record:batch.records) {
              state.rtActivity.observe(record);
              if(pending.size()==config.displayCapacity){pending.pop_front();++state.displayDrops;}
              pending.push_back(record);
            }
            state.queueDepth=pending.size();state.queueHighWater=std::max(state.queueHighWater,pending.size());
            for(unsigned k=0;k<TimingStageCount;++k)if(durations[k])state.timings[k].observe(durations[k]);
          }
          if(entry)displayQueue.release(entry.bytes);
        }
        std::lock_guard lock(mutex);
        if(decoderRevision==profileRevision)live=std::make_shared<LiveFrame>(decoder.snapshot());
        rtLive=std::make_shared<RtMonitorFrame>(rtMonitor);
      } catch(const std::exception &) {
        displayFailed=true;
        displayQueue.close();
      }
    };
    bool announced=false;
    try {
      if(!events) throw std::runtime_error("Cannot create capture diagnostic log");
      std::string enabled;for(bool selected:config.enabled) enabled+=selected ? '1' : '0';
      emit("capture_requested","\"source\":"+jsonString(config.hardware ? "DDC_HARDWARE" : "SIMULATOR")+",\"logical_devices\":["+std::to_string(config.logicalDevices[0])+","+std::to_string(config.logicalDevices[1])+"],\"bus_mask\":"+jsonString(enabled)+",\"display_capacity\":"+std::to_string(config.displayCapacity)+",\"chapter10_tick_ns\":2000,\"native_timebase_requested_ns\":2000");
      const auto parent=path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
      std::error_code error;
      const auto space=std::filesystem::space(parent,error);
      if(error || space.available<100*1024*1024) throw std::runtime_error("Recording directory unavailable or less than 100 MB free");
      if(!reuse) {if(auto message=source->open(config);!message.empty()) throw std::runtime_error(message);}
      source->updateCallbacks(config);
      const auto sourceIdentity=source->identity();
      {std::lock_guard lock(mutex);state.identity=sourceIdentity;}
      emit("connection_identity","\"identity\":"+identityJson(sourceIdentity));
      connectedFlag=true;
      if(!events) throw std::runtime_error("Capture diagnostic write failed");
      if(stop.stop_requested()) throw std::runtime_error("Connection cancelled");
      if(!writer.open(path,true))throw std::runtime_error("Cannot create a new recording at the selected path");
      auto timelinePath=path;timelinePath+=".icd.jsonl";
      profileTimeline.open(timelinePath,std::ios::trunc);
      if(!profileTimeline) throw std::runtime_error("Cannot create ICD revision timeline");
      {std::lock_guard lock(mutex);activeMappings=requestedProfile;decoderRevision=profileRevision;}
      decoder.configure(activeMappings,decoderRevision,!config.hardware);
      if(auto problem=validateChecks(config.checkRules,activeMappings);!problem.empty()) throw std::runtime_error(problem);
      if(!checkLog) throw std::runtime_error("Cannot create signal check log");
      checks.configure(activeMappings,config.checkRules,!config.hardware);
      {
        auto configurationPath=path;configurationPath+=".configuration.json";std::ofstream configuration(configurationPath);
        configuration.imbue(std::locale::classic());configuration<<std::setprecision(17);
        configuration<<"{\"schema\":\"aerotrace.capture-config.v1\",\"run_id\":"<<jsonString(config.runId)<<",\"hardware\":"<<(config.hardware?"true":"false")<<",\"profile\":"<<serializeProfile(activeMappings)<<",\"simulator\":{\"fixture_id\":"<<jsonString(config.hardware?"":offlineFixtureId)<<",\"rate\":"<<config.simulatorRate<<"},\"checks\":[";
        for(std::size_t i=0;i<config.checkRules.size();++i){const auto &r=config.checkRules[i];if(i)configuration<<',';configuration<<"{\"signal\":"<<jsonString(r.signalId)<<",\"missing_ns\":\""<<r.missingNs<<"\",\"hysteresis\":"<<r.hysteresis;if(r.minimum)configuration<<",\"minimum\":"<<*r.minimum;if(r.maximum)configuration<<",\"maximum\":"<<*r.maximum;configuration<<'}';}
        configuration<<"],\"ddc\":{\"backend\":"<<jsonString(config.backend==DdcBackend::HelperX86?"helper_x86":"native_x64")
          <<",\"receive_mode\":"<<jsonString(config.receiveMode==DdcReceiveMode::MTI?"MTI":config.receiveMode==DdcReceiveMode::LegacyMT?"LegacyMT":"Auto")
          <<",\"requested_connection_policy\":"<<jsonString(config.connectionPolicy==DdcConnectionPolicy::Compatibility?"compatibility":"qualified_automatic")
          <<",\"runtime\":"<<jsonString(pathUtf8(config.runtime))<<",\"helper_pack\":"<<jsonString(pathUtf8(config.helperPack))
          <<",\"resolved_identity\":"<<identityJson(sourceIdentity)
          <<"},\"rt_statistics\":{\"rules\":\"aerotrace.rt-statistics.v1\",\"unsupported_categories\":\"unavailable\",\"provider_identity\":\"ddc_api/provider_selected event\"}}";configuration.flush();if(!configuration)throw std::runtime_error("Cannot preserve capture/check configuration");
      }
      {auto snapshotPath=path;snapshotPath+=".profile.json";std::ofstream snapshot(snapshotPath);snapshot<<serializeProfile(activeMappings);snapshot.flush();if(!snapshot) throw std::runtime_error("Cannot preserve initial ICD profile");}
      if(stop.stop_requested())throw std::runtime_error("Recording start cancelled");
      const auto initialSignals=activeMappings.size();
      diskWorker=std::jthread(runWriter);
      displayWorker=std::jthread(runDisplay);
      if(auto message=source->startMonitoring();!message.empty()) throw std::runtime_error(message);
      for(unsigned channel=0;channel<2;++channel) if(config.enabled[channel*2]||config.enabled[channel*2+1])
        config.deviceState(channel,config.hardware ? DevicePhase::Monitoring : DevicePhase::Simulating,{});
      running=true;started.set_value(true);announced=true;
      emit("capture_started","\"signals\":"+std::to_string(initialSignals));
      auto previousPoll=steady_clock::now();
      auto lastHealth=previousPoll;
      std::uint64_t ingressBytes=0;
      auto observeSource=[&](const SourceBatch &batch) {
        std::lock_guard lock(mutex);
        ++state.batches;
        state.hardwareLost=std::max(state.hardwareLost,batch.hardwareLost);
        state.hardwareOverflows=std::max(state.hardwareOverflows,batch.hardwareOverflows);
        for(unsigned c=0;c<2;++c) {
          state.channelLost[c]=std::max(state.channelLost[c],batch.channelLost[c]);
          state.channelOverflows[c]=std::max(state.channelOverflows[c],batch.channelOverflows[c]);
        }
        if(batch.pollNs)state.timings[SdkPoll].observe(batch.pollNs);
        if(batch.packetProcessingNs)state.timings[PacketProcessing].observe(batch.packetProcessingNs);
      };
      while(!stop.stop_requested() && !diskFailed) {
        const auto began=steady_clock::now();
        SourceBatch batch;
        const auto sourceError=source->poll(batch,stop);
        observeSource(batch);
        {
          std::lock_guard lock(mutex);
          const auto interval=duration<double,std::milli>(began-previousPoll).count();
          state.maxBatchMilliseconds=std::max(state.maxBatchMilliseconds,interval);
          state.timings[WorkerBatch].observe(duration_cast<nanoseconds>(began-previousPoll).count());
        }
        previousPoll=began;
        const bool loss=batch.hardwareLost || batch.hardwareOverflows;
        if(!batch.records.empty()||!batch.packets.empty())ingressBytes+=captureBatchBytes(batch);
        enqueue(std::move(batch));
        if(loss)throw std::runtime_error("DDC reported lost messages or stack overflow; recording is incomplete");
        if(!sourceError.empty())throw std::runtime_error(sourceError);
        if(began-lastHealth>=seconds(1)) {
          const auto rate=std::uint64_t(ingressBytes/duration<double>(began-lastHealth).count());
          {std::lock_guard lock(mutex);state.peakIngressBytesPerSecond=std::max(state.peakIngressBytesPerSecond,rate);}
          const auto health=metrics(false);
          std::ostringstream fields;
          fields<<"\"recorded\":"<<health.recorded<<",\"checkpointed\":"<<health.checkpointed
            <<",\"writer_queue_bytes\":"<<health.writerQueueBytes<<",\"writer_queue_high_water\":"<<health.writerQueueHighWater
            <<",\"display_skipped\":"<<health.displayDrops<<",\"hardware_lost\":"<<health.hardwareLost
            <<",\"hardware_overflows\":"<<health.hardwareOverflows<<",\"peak_ingress_bytes_per_second\":"<<health.peakIngressBytesPerSecond
            <<",\"queue_depth\":"<<health.queueDepth<<",\"queue_high_water\":"<<health.queueHighWater
            <<",\"max_batch_ms\":"<<health.maxBatchMilliseconds<<",\"clocks\":[";
          for(unsigned i=0;i<health.clocks.size();++i) {
            const auto &clock=health.clocks[i];
            if(i)fields<<',';
            fields<<"{\"stream\":"<<i<<",\"count\":"<<clock.count<<",\"backwards\":"<<clock.backwards
                  <<",\"max_forward_gap_ns\":"<<clock.maxForwardGapNs<<'}';
          }
          fields<<']';
          emit("health",fields.str());
          lastHealth=began;ingressBytes=0;
        }
      }
    } catch(const std::exception &error) { fail(error.what()); }
    if(announced) {
      try {
        if(auto message=source->stopMonitoring();!message.empty())fail(message);
        bool more=true;
        const auto drainDeadline=steady_clock::now()+seconds(15);
        while(more) {
          SourceBatch tail;
          const auto message=source->drainNext(tail,more);
          const bool loss=tail.hardwareLost || tail.hardwareOverflows;
          {
            std::lock_guard lock(mutex);
            state.hardwareLost=std::max(state.hardwareLost,tail.hardwareLost);
            state.hardwareOverflows=std::max(state.hardwareOverflows,tail.hardwareOverflows);
            for(unsigned c=0;c<2;++c) {
              state.channelLost[c]=std::max(state.channelLost[c],tail.channelLost[c]);
              state.channelOverflows[c]=std::max(state.channelOverflows[c],tail.channelOverflows[c]);
            }
          }
          enqueue(std::move(tail));
          if(loss)fail("DDC stop drain reported incomplete capture");
          if(!message.empty()){fail(message);break;}
          if(more && steady_clock::now()>=drainDeadline){fail("Stop/drain deadline exceeded; recording is incomplete");break;}
        }
      } catch(const std::exception &error){fail(error.what());}
    }
    rawQueue.close();
    displayQueue.close();
    if(diskWorker.joinable())diskWorker.join();
    if(displayWorker.joinable())displayWorker.join();
    if(!diskError.empty())fail(diskError);
    if(displayFailed){std::lock_guard lock(mutex);state.analysisUnavailable=true;}
    {
      std::lock_guard lock(mutex);
      state.writerQueueBytes=0;
      if(state.writerRejected && !state.failed){state.failed=true;state.error="Recording queue rejected data";}
    }
    if(!announced) started.set_value(false);
    if(!retainConnection || error()) {
      source->close();connectedFlag=false;
      if(auto message=source->closeError();!message.empty()) {
        cleanupFault=true;
        {std::lock_guard lock(mutex);state.cleanupBlocked=true;}
        fail(message);
      }
    }
    {std::lock_guard lock(mutex);for(auto &device:state.devices)
      if(device.phase==DevicePhase::Monitoring || device.phase==DevicePhase::Simulating) device={DevicePhase::Stopped,{}};}
    const auto closing=metrics();
    emit("capture_closed","\"recorded\":"+std::to_string(closing.recorded)+",\"display_skipped\":"+std::to_string(closing.displayDrops)+",\"hardware_lost\":"+std::to_string(closing.hardwareLost)+",\"hardware_overflows\":"+std::to_string(closing.hardwareOverflows)+",\"decoded_values\":"+std::to_string(closing.decodedValues)+",\"invalid_values\":"+std::to_string(closing.invalidValues)+",\"failed\":"+(error() ? "true" : "false"));
    writeJournal();events.flush();
    if(!events){std::lock_guard lock(mutex);++state.diagnosticSkipped;}
    const auto final=metrics();
    auto statusPath=path;statusPath+=".status.txt";
    std::ofstream status(statusPath);
    status<<"source="<<(config.hardware ? "DDC_HARDWARE" : "SIMULATOR")<<"\nrecorded="<<final.recorded
      <<"\nhardware_lost="<<final.hardwareLost<<"\nhardware_overflows="<<final.hardwareOverflows
      <<"\ndisplay_skipped="<<final.displayDrops<<"\nanalysis_skipped="<<final.analysisSkipped
      <<"\nanalysis_unavailable="<<final.analysisUnavailable<<"\ncheckpointed="<<final.checkpointed
      <<"\nwriter_rejected="<<final.writerRejected<<"\nwriter_queue_high_water_bytes="<<final.writerQueueHighWater
      <<"\npeak_ingress_bytes_per_second="<<final.peakIngressBytesPerSecond
      <<"\ndiagnostic_skipped="<<final.diagnosticSkipped
      <<"\nqueue_high_water="<<final.queueHighWater
      <<"\nintegrity="<<(final.failed ? "INCOMPLETE" : "CLOSED")<<"\nerror="<<final.error<<'\n';
    for(unsigned i=0;i<4;++i) status<<"stream_"<<i<<"_clock_backwards="<<final.clocks[i].backwards<<"\nstream_"<<i<<"_max_forward_gap_ns="<<final.clocks[i].maxForwardGapNs<<'\n';
    for(unsigned c=0;c<2;++c)status<<"channel_"<<c+1<<"_loss_observed="<<final.channelLost[c]<<"\nchannel_"<<c+1<<"_overflow_observed="<<final.channelOverflows[c]<<'\n';
    status<<"counter_semantics=observed per-channel high-water; vendor cumulative/reset behavior unverified\n";
    status.flush();if(!status)fail("Cannot preserve final capture status");
    if(retainConnection && !error()) {
      source->updateCallbacks(CaptureConfig{});
      retainedConfig=config;retainedConfig.deviceState={};retainedConfig.diagnostic={};retainedConfig.stopToken={};
      retainedSource=std::move(source);connectedFlag=true;
      std::lock_guard lock(mutex);for(auto &device:state.devices)if(device.phase==DevicePhase::Stopped)device={DevicePhase::Ready,"Connected; recording stopped"};
    } else if(source) {source->close();connectedFlag=false;
      if(auto message=source->closeError();!message.empty()){cleanupFault=true;{std::lock_guard lock(mutex);state.cleanupBlocked=true;}fail(message);}
    }
    running=false;
  });
  return ready;
}
void Acquisition::requestStop() { if(worker.joinable()) worker.request_stop(); }
void Acquisition::stopRecording() {retainConnection=true;if(worker.joinable()){worker.request_stop();worker.join();}running=false;}
void Acquisition::stop() {
  retainConnection=false;if(worker.joinable()){worker.request_stop();worker.join();}running=false;
  std::string closeFailure;
  if(retainedSource){retainedSource->close();closeFailure=retainedSource->closeError();retainedSource.reset();}
  connectedFlag=false;
  std::lock_guard lock(mutex);if(!closeFailure.empty()){cleanupFault=true;state.failed=true;state.cleanupBlocked=true;state.error+=(state.error.empty()?"":"; ")+closeFailure;
    if(!state.failure){state.failure.category=CaptureFailureCategory::Cleanup;state.failure.stage="close";state.failure.operation="close";state.failure.detail=closeFailure;state.failure.recovery={"Restart AeroTrace after checking the driver and channel owner"};}
  }for(auto &device:state.devices)if(device.phase!=DevicePhase::Failed&&device.phase!=DevicePhase::Disabled&&!(state.failed&&device.phase==DevicePhase::Stopped))device={DevicePhase::Stopped,{}};
}
bool Acquisition::connected() const {return connectedFlag;}
std::vector<Record> Acquisition::drain(std::size_t maximum) {
  std::lock_guard lock(mutex);
  std::vector<Record> records;records.reserve(std::min(maximum,pending.size()));
  while(!pending.empty() && records.size()<maximum) { records.push_back(std::move(pending.front()));pending.pop_front(); }
  state.queueDepth=pending.size();return records;
}
bool Acquisition::active() const { return running; }
bool Acquisition::error() const { std::lock_guard lock(mutex);return state.failed; }
std::uint64_t Acquisition::displayDrops() const { std::lock_guard lock(mutex);return state.displayDrops; }
CaptureMetrics Acquisition::metrics(bool includeEvents) const { std::lock_guard lock(mutex);
  if(includeEvents)return state;
  CaptureMetrics result;result.checkpointed=state.checkpointed;result.writerRejected=state.writerRejected;
  result.writerQueueBytes=state.writerQueueBytes;result.writerQueueHighWater=state.writerQueueHighWater;
  result.analysisSkipped=state.analysisSkipped;result.analysisUnavailable=state.analysisUnavailable;
  result.diagnosticSkipped=state.diagnosticSkipped;result.peakIngressBytesPerSecond=state.peakIngressBytesPerSecond;
  result.recorded=state.recorded;result.displayDrops=state.displayDrops;result.hardwareLost=state.hardwareLost;result.hardwareOverflows=state.hardwareOverflows;result.batches=state.batches;result.queueDepth=state.queueDepth;result.queueHighWater=state.queueHighWater;result.maxBatchMilliseconds=state.maxBatchMilliseconds;result.timings=state.timings;result.decodedValues=state.decodedValues;result.invalidValues=state.invalidValues;result.sourceCounts=state.sourceCounts;result.lastTrafficNs=state.lastTrafficNs;result.channelLost=state.channelLost;result.channelOverflows=state.channelOverflows;result.devices=state.devices;result.configuredChannels=state.configuredChannels;result.clocks=state.clocks;result.rtActivity=state.rtActivity;result.hardware=state.hardware;result.failed=state.failed;result.error=state.error;result.failure=state.failure;result.identity=state.identity;result.cleanupBlocked=state.cleanupBlocked;result.checkCount=state.checkCount;return result;
}
std::string Acquisition::setProfile(std::vector<Signal> profile) {
  if(auto error=validateProfile(profile);!error.empty()) return error;
  std::lock_guard lock(mutex);requestedProfile=std::move(profile);++profileRevision;live.reset();return {};
}
std::shared_ptr<const LiveFrame> Acquisition::liveFrame() const {std::lock_guard lock(mutex);return live;}
std::shared_ptr<const RtMonitorFrame> Acquisition::rtFrame() const {std::lock_guard lock(mutex);return rtLive;}
void Acquisition::setTraceSignals(std::vector<unsigned> selected) {
  std::vector<unsigned> wanted;for(auto index:selected) if(index<512 && wanted.size()<8 && std::find(wanted.begin(),wanted.end(),index)==wanted.end()) wanted.push_back(index);
  std::lock_guard lock(mutex);if(wanted!=requestedTraces) {requestedTraces=std::move(wanted);++traceRevision;}
}
void Acquisition::setCommandPlot(CommandPlotSelection selection) {
  std::lock_guard lock(mutex);
  if(selection!=requestedCommandPlot) {requestedCommandPlot=std::move(selection);++traceRevision;}
}
}
