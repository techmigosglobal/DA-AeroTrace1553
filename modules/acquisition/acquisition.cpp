#include "acquisition.hpp"
#include <algorithm>
#include <chrono>
#include <future>
#include <stdexcept>
#include <iomanip>
#include <locale>

namespace aero {
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
  {std::lock_guard lock(mutex);state={};pending.clear();live.reset();rtLive.reset();state.hardware=config.hardware;}
  std::promise<bool> completion;auto ready=completion.get_future();
  if(!source)source=config.hardware?makeDdcSource():makeSimulator();
  worker=std::jthread([this,config,source=std::move(source),completion=std::move(completion)](std::stop_token token) mutable {
    config.stopToken=token;
    const auto userState=config.deviceState;
    config.deviceState=[this,userState](unsigned channel,DevicePhase phase,const std::string &message) {
      if(channel<2){std::lock_guard lock(mutex);state.devices[channel]={phase,message};}
      if(userState)userState(channel,phase,message);
    };
    std::string message;
    try {
      if(std::none_of(config.enabled.begin(),config.enabled.end(),[](bool on){return on;}))throw std::runtime_error("Enable at least one channel/bus");
      for(unsigned c=0;c<2;++c)config.deviceState(c,config.enabled[c*2]||config.enabled[c*2+1]?DevicePhase::Checking:DevicePhase::Disabled,{});
      message=source->open(config);
      if(message.empty()&&token.stop_requested())message="Connection cancelled";
    }catch(const std::exception &e){message=e.what();}
    if(!message.empty()) {
      source->close();
      std::lock_guard lock(mutex);state.failed=true;state.error=message;
      for(unsigned c=0;c<2;++c)if(config.enabled[c*2]||config.enabled[c*2+1])state.devices[c]={DevicePhase::Failed,message};
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
    retainedConfig.enabled==config.enabled && retainedConfig.logicalDevices==config.logicalDevices && retainedConfig.runtime==config.runtime;
  if(reuse)source=std::move(retainedSource);
  else {if(retainedSource){retainedSource->close();retainedSource.reset();}connectedFlag=false;}
  retainConnection=true;
  {
    std::lock_guard lock(mutex);state={};pending.clear();live.reset();rtLive.reset();state.hardware=config.hardware;
  }
  if(config.displayCapacity<1 || config.displayCapacity>65536 ||
     std::none_of(config.enabled.begin(),config.enabled.end(),[](bool on){return on;})) {
    {std::lock_guard lock(mutex);state.failed=true;state.error="Enable at least one stream and use a display capacity of 1–65536";}
    std::promise<bool> rejected;auto ready=rejected.get_future();rejected.set_value(false);return ready;
  }
  if(!source) source=config.hardware ? makeDdcSource() : makeSimulator();
  std::promise<bool> started;auto ready=started.get_future();
  worker=std::jthread([this,path,config,reuse,source=std::move(source),started=std::move(started)](std::stop_token stop) mutable {
    using namespace std::chrono;
    config.stopToken=stop;
    const auto utf8=path.u8string();const std::string captureName(utf8.begin(),utf8.end());
    auto eventPath=path;eventPath+=".events.jsonl";std::ofstream events(eventPath,std::ios::trunc);
    auto emit=[&](const std::string &event,const std::string &fields) {
      events<<"{\"utc_epoch_ms\":"<<duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
        <<",\"host_steady_ns\":"<<duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count()
        <<",\"run_id\":"<<jsonString(config.runId)<<",\"capture\":"<<jsonString(captureName)<<",\"event\":"<<jsonString(event)<<','<<fields<<"}\n";events.flush();
    };
    config.diagnostic=[&](const std::string &operation,int result,const std::string &detail) {
      emit("ddc_api","\"operation\":"+jsonString(operation)+",\"result\":"+std::to_string(result)+",\"detail\":"+jsonString(detail));
    };
    config.deviceState=[this,&emit](unsigned channel,DevicePhase phase,const std::string &message) {
      emit("device_state","\"channel\":"+std::to_string(channel+1)+",\"phase\":"+std::to_string(int(phase))+",\"detail\":"+jsonString(message));
      if(channel>=2) return;std::lock_guard lock(mutex);state.devices[channel]={phase,message};
    };
    for(unsigned channel=0;channel<2;++channel)
      config.deviceState(channel,config.enabled[channel*2]||config.enabled[channel*2+1] ? DevicePhase::Checking : DevicePhase::Disabled,{});
    Recorder writer;
    LiveDecoder decoder;
    RtMonitorFrame rtMonitor;rtMonitor.session=captureName;
    std::uint64_t decoderRevision=UINT64_MAX;
    std::uint64_t decoderTraceRevision=UINT64_MAX;
    std::array<std::ofstream,2> packets;
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
    bool revisionPending=true;
    auto fail=[this,&emit](const std::string &message) { emit("capture_error","\"detail\":"+jsonString(message));std::lock_guard lock(mutex);
      if(!state.failed)state.error=message;else if(state.error.find(message)==std::string::npos&&state.error.size()<8192)state.error+="; "+message;state.failed=true;
      for(auto &device:state.devices) {
        if(device.phase==DevicePhase::Ready) device={DevicePhase::Stopped,"Configuration closed: the other channel is not ready"};
        else if(device.phase==DevicePhase::Checking || device.phase==DevicePhase::Monitoring) device={DevicePhase::Failed,message};
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
      connectedFlag=true;
      if(!events) throw std::runtime_error("Capture diagnostic write failed");
      if(stop.stop_requested()) throw std::runtime_error("Connection cancelled");
      if(!writer.open(path)) throw std::runtime_error("Cannot create recording");
      auto timelinePath=path;timelinePath+=".icd.jsonl";
      profileTimeline.open(timelinePath,std::ios::trunc);
      if(!profileTimeline) throw std::runtime_error("Cannot create ICD revision timeline");
      {std::lock_guard lock(mutex);activeMappings=requestedProfile;decoderRevision=profileRevision;}
      decoder.configure(activeMappings,decoderRevision);
      if(auto problem=validateChecks(config.checkRules,activeMappings);!problem.empty()) throw std::runtime_error(problem);
      if(!checkLog) throw std::runtime_error("Cannot create signal check log");
      checks.configure(activeMappings,config.checkRules);
      {
        auto configurationPath=path;configurationPath+=".configuration.json";std::ofstream configuration(configurationPath);
        configuration.imbue(std::locale::classic());configuration<<std::setprecision(17);
        configuration<<"{\"schema\":\"aerotrace.capture-config.v1\",\"run_id\":"<<jsonString(config.runId)<<",\"hardware\":"<<(config.hardware?"true":"false")<<",\"profile\":"<<serializeProfile(activeMappings)<<",\"simulator\":{\"rate\":"<<config.simulatorRate<<",\"wave\":"<<jsonString(config.simulatorWave)<<",\"amplitude\":"<<config.simulatorAmplitude<<",\"offset\":"<<config.simulatorOffset<<",\"frequency\":"<<config.simulatorFrequency<<",\"seed\":\""<<config.simulatorSeed<<"\",\"drop_every\":"<<config.simulatorDropEvery<<",\"flag_every\":"<<config.simulatorFlagEvery<<"},\"checks\":[";
        for(std::size_t i=0;i<config.checkRules.size();++i){const auto &r=config.checkRules[i];if(i)configuration<<',';configuration<<"{\"signal\":"<<jsonString(r.signalId)<<",\"missing_ns\":\""<<r.missingNs<<"\",\"hysteresis\":"<<r.hysteresis;if(r.minimum)configuration<<",\"minimum\":"<<*r.minimum;if(r.maximum)configuration<<",\"maximum\":"<<*r.maximum;configuration<<'}';}
        configuration<<"]}";configuration.flush();if(!configuration)throw std::runtime_error("Cannot preserve capture/check configuration");
      }
      {auto snapshotPath=path;snapshotPath+=".profile.json";std::ofstream snapshot(snapshotPath);snapshot<<serializeProfile(activeMappings);snapshot.flush();if(!snapshot) throw std::runtime_error("Cannot preserve initial ICD profile");}
      if(stop.stop_requested())throw std::runtime_error("Recording start cancelled");
      if(auto message=source->startMonitoring();!message.empty()) throw std::runtime_error(message);
      for(unsigned channel=0;channel<2;++channel) if(config.enabled[channel*2]||config.enabled[channel*2+1])
        config.deviceState(channel,config.hardware ? DevicePhase::Monitoring : DevicePhase::Simulating,{});
      running=true;started.set_value(true);announced=true;
      emit("capture_started","\"profile_revision\":"+std::to_string(decoderRevision)+",\"signals\":"+std::to_string(activeMappings.size()));
      auto flushed=steady_clock::now(),checked=flushed,published=flushed-milliseconds(50);
      while(!stop.stop_requested()) {
        const auto began=steady_clock::now();
        std::vector<Signal> replacement;
        std::vector<unsigned> traces;bool changeTraces=false;
        bool replace=false;
        {
          std::lock_guard lock(mutex);
          if(decoderRevision!=profileRevision) {replacement=requestedProfile;decoderRevision=profileRevision;replace=true;}
          if(replace || decoderTraceRevision!=traceRevision) {traces=requestedTraces;decoderTraceRevision=traceRevision;changeTraces=true;}
        }
        if(replace) {activeMappings=replacement;decoder.configure(std::move(replacement),decoderRevision);revisionPending=true;
          if(auto problem=validateChecks(config.checkRules,activeMappings);!problem.empty()) {config.checkRules.clear();emit("checks_reset","\"reason\":"+jsonString(problem));}
          checks.configure(activeMappings,config.checkRules);lastHost={};
          emit("profile_activated","\"revision\":"+std::to_string(decoderRevision)+",\"signals\":"+std::to_string(activeMappings.size()));}
        if(changeTraces) {decoder.selectTraces(traces);std::string indices;for(auto index:traces) {if(!indices.empty()) indices+=',';indices+=std::to_string(index);}emit("trace_selection","\"signal_indices\":["+indices+"]");}
        SourceBatch batch;
        const auto sourceError=source->poll(batch,stop);
        std::array<std::uint64_t,TimingStageCount> durations{};auto timedAt=steady_clock::now();
        durations[SdkPoll]=config.hardware?(batch.pollNs?batch.pollNs:duration_cast<nanoseconds>(timedAt-began).count()):0;durations[PacketProcessing]=batch.packetProcessingNs;
        auto measured=[&](TimingStage stage){const auto end=steady_clock::now();durations[stage]+=duration_cast<nanoseconds>(end-timedAt).count();timedAt=end;};
        for(const auto &packet:batch.packets) {
          if(packet.channel<1 || packet.channel>2) throw std::runtime_error("Invalid source packet channel");
          auto &output=packets[packet.channel-1];
          if(!output.is_open()){auto raw=path;raw+=std::string(".ch")+std::to_string(packet.channel)+(packet.chapter10?".ch10":".mt40");output.open(raw,std::ios::binary|std::ios::trunc);}
          output.write(reinterpret_cast<const char *>(packet.bytes.data()),packet.bytes.size());
          if(!output) throw std::runtime_error("Raw DDC packet write failed");
        }
        measured(PacketWrite);
        if(revisionPending && !batch.records.empty()) {
          const auto profile=serializeProfile(activeMappings);
          profileTimeline<<"{\"revision\":"<<decoderRevision<<",\"first_record\":"<<(batch.records.empty() ? 0 : batch.records.front().id)<<",\"profile\":"<<profile<<"}\n";
          profileTimeline.flush();
          auto profilePath=path;profilePath+=".profile.json";
          std::ofstream snapshot(profilePath,std::ios::trunc);snapshot<<profile;snapshot.flush();
          if(!profileTimeline || !snapshot) throw std::runtime_error("Cannot preserve ICD profile provenance");
          revisionPending=false;
        }
        timedAt=steady_clock::now();
        if(!writer.appendBatch(batch.records)) throw std::runtime_error("Recording write failed");
        measured(RecordingWrite);decoder.consume(batch.records);measured(Conversion);
        for(const auto &r:batch.records)rtMonitor.consume(r);measured(RtAggregation);
        const auto now=steady_clock::now();
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
        if(now-published>=milliseconds(50) || replace || changeTraces) {
          snapshot=std::make_shared<LiveFrame>(decoder.snapshot());rtSnapshot=std::make_shared<RtMonitorFrame>(rtMonitor);published=now;
        }
        measured(Publication);
        if(now-flushed>=milliseconds(100)) {
          if(!writer.flush()) throw std::runtime_error("Recording flush failed");
          for(auto &output:packets) if(output.is_open()) { output.flush();if(!output) throw std::runtime_error("DDC packet flush failed"); }
          flushed=now;
        }
        measured(Flush);
        const bool healthDue=now-checked>=seconds(1);
        if(healthDue) {
          const auto free=std::filesystem::space(parent,error);
          if(error || free.available<100*1024*1024) throw std::runtime_error("Recording stopped: low disk space");
          checked=now;
        }
        {
          std::lock_guard lock(mutex);
          const auto publicationBegan=steady_clock::now();
          state.recorded+=batch.records.size();++state.batches;
          state.decodedValues=decoder.conversions();state.invalidValues=decoder.invalidConversions();
          if(snapshot && snapshot->revision==profileRevision) live=std::move(snapshot);
          if(rtSnapshot)rtLive=std::move(rtSnapshot);
          state.hardwareLost=batch.hardwareLost;state.hardwareOverflows=batch.hardwareOverflows;
          state.channelLost=batch.channelLost;state.channelOverflows=batch.channelOverflows;
          for(auto &record:batch.records) {
            state.rtActivity.observe(record);
            if(record.channel>=1 && record.channel<=2 && record.bus<2) {const auto stream=(record.channel-1)*2+record.bus;++state.sourceCounts[stream];state.clocks[stream].observe(record.timestampNs);}
            if(pending.size()==config.displayCapacity) { pending.pop_front();++state.displayDrops; }
            pending.push_back(std::move(record));
          }
          state.queueDepth=pending.size();state.queueHighWater=std::max(state.queueHighWater,pending.size());
          state.maxBatchMilliseconds=std::max(state.maxBatchMilliseconds,duration<double,std::milli>(steady_clock::now()-began).count());
          durations[Publication]+=duration_cast<nanoseconds>(steady_clock::now()-publicationBegan).count();
          durations[WorkerBatch]=duration_cast<nanoseconds>(steady_clock::now()-began).count();
          for(unsigned k=0;k<TimingStageCount;++k)if(durations[k])state.timings[k].observe(durations[k]);
        }
        if(healthDue) {
          const auto health=metrics();std::ostringstream fields;
          fields<<"\"recorded\":"<<health.recorded<<",\"display_skipped\":"<<health.displayDrops<<",\"hardware_lost\":"<<health.hardwareLost<<",\"hardware_overflows\":"<<health.hardwareOverflows<<",\"queue_depth\":"<<health.queueDepth<<",\"queue_high_water\":"<<health.queueHighWater<<",\"max_batch_ms\":"<<health.maxBatchMilliseconds<<",\"clocks\":[";
          for(unsigned i=0;i<4;++i) {if(i) fields<<',';const auto &clock=health.clocks[i];fields<<"{\"stream\":"<<i<<",\"count\":"<<clock.count<<",\"first_ns\":"<<clock.firstNs<<",\"last_ns\":"<<clock.lastNs<<",\"backwards\":"<<clock.backwards<<",\"repeated\":"<<clock.repeated<<",\"max_forward_gap_ns\":"<<clock.maxForwardGapNs<<'}';}
          fields<<"],\"decoded_values\":"<<health.decodedValues<<",\"invalid_values\":"<<health.invalidValues;emit("health",fields.str());
          if(!events) throw std::runtime_error("Capture diagnostic write failed");
        }
        if(batch.hardwareLost || batch.hardwareOverflows) throw std::runtime_error("DDC reported lost messages or stack overflow; session is incomplete");
        if(!sourceError.empty()) throw std::runtime_error(sourceError);
      }
    } catch(const std::exception &error) { fail(error.what()); }
    if(announced) {
      SourceBatch tail;
      std::string message;
      try {message=source->finish(tail);}catch(const std::exception &e){message=e.what();}
      for(const auto &packet:tail.packets) if(packet.channel>=1 && packet.channel<=2) {
        auto &output=packets[packet.channel-1];
        if(!output.is_open()){auto raw=path;raw+=std::string(".ch")+std::to_string(packet.channel)+(packet.chapter10?".ch10":".mt40");output.open(raw,std::ios::binary|std::ios::trunc);}
        output.write(reinterpret_cast<const char *>(packet.bytes.data()),packet.bytes.size());
        if(!output) fail("Final DDC packet write failed");
      }
      if(!writer.appendBatch(tail.records)) fail("Final recording batch failed");
      try {for(const auto &record:tail.records) {checks.consume(record);publishChecks();}} catch(const std::exception &e) {fail(e.what());}
      decoder.consume(tail.records);
      for(const auto &r:tail.records)rtMonitor.consume(r);
      {
        std::lock_guard lock(mutex);state.recorded+=tail.records.size();
        state.decodedValues=decoder.conversions();state.invalidValues=decoder.invalidConversions();
        for(auto &record:tail.records) {
          state.rtActivity.observe(record);
          if(record.channel>=1 && record.channel<=2 && record.bus<2) {const auto stream=(record.channel-1)*2+record.bus;++state.sourceCounts[stream];state.clocks[stream].observe(record.timestampNs);}
          if(pending.size()==config.displayCapacity) {pending.pop_front();++state.displayDrops;}
          pending.push_back(std::move(record));
        }
        state.queueDepth=pending.size();state.queueHighWater=std::max(state.queueHighWater,pending.size());
        state.hardwareLost=std::max(state.hardwareLost,tail.hardwareLost);state.hardwareOverflows=std::max(state.hardwareOverflows,tail.hardwareOverflows);
        for(unsigned c=0;c<2;++c){state.channelLost[c]=std::max(state.channelLost[c],tail.channelLost[c]);state.channelOverflows[c]=std::max(state.channelOverflows[c],tail.channelOverflows[c]);}
      }
      if(!message.empty()) fail(message);
      if(tail.hardwareLost || tail.hardwareOverflows) fail("DDC stop drain reported incomplete capture");
    }
    if(!announced) started.set_value(false);
    if(!retainConnection || error()) {
      source->close();connectedFlag=false;
      if(auto message=source->closeError();!message.empty())fail(message);
    }
    {std::lock_guard lock(mutex);for(auto &device:state.devices)
      if(device.phase==DevicePhase::Monitoring || device.phase==DevicePhase::Simulating) device={DevicePhase::Stopped,{}};}
    {std::lock_guard lock(mutex);if(decoderRevision==profileRevision) live=std::make_shared<LiveFrame>(decoder.snapshot());rtLive=std::make_shared<RtMonitorFrame>(rtMonitor);}
    if(!writer.close()) fail("Final recording flush failed");
    for(auto &output:packets) if(output.is_open()) { output.flush();if(!output) fail("Final raw packet flush failed");output.close(); }
    running=false;
    const auto closing=metrics();
    emit("capture_closed","\"recorded\":"+std::to_string(closing.recorded)+",\"display_skipped\":"+std::to_string(closing.displayDrops)+",\"hardware_lost\":"+std::to_string(closing.hardwareLost)+",\"hardware_overflows\":"+std::to_string(closing.hardwareOverflows)+",\"decoded_values\":"+std::to_string(closing.decodedValues)+",\"invalid_values\":"+std::to_string(closing.invalidValues)+",\"failed\":"+(error() ? "true" : "false"));
    if(!events) fail("Capture diagnostic write failed");
    const auto final=metrics();
    auto statusPath=path;statusPath+=".status.txt";
    std::ofstream status(statusPath);
    status<<"source="<<(config.hardware ? "DDC_HARDWARE" : "SIMULATOR")<<"\nrecorded="<<final.recorded
      <<"\nhardware_lost="<<final.hardwareLost<<"\nhardware_overflows="<<final.hardwareOverflows
      <<"\ndisplay_skipped="<<final.displayDrops<<"\nqueue_high_water="<<final.queueHighWater
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
    } else if(source) {source->close();connectedFlag=false;}
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
  std::lock_guard lock(mutex);if(!closeFailure.empty()){state.failed=true;state.error=closeFailure;}for(auto &device:state.devices)if(device.phase!=DevicePhase::Failed&&device.phase!=DevicePhase::Disabled)device={DevicePhase::Stopped,{}};
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
  CaptureMetrics result;result.recorded=state.recorded;result.displayDrops=state.displayDrops;result.hardwareLost=state.hardwareLost;result.hardwareOverflows=state.hardwareOverflows;result.batches=state.batches;result.queueDepth=state.queueDepth;result.queueHighWater=state.queueHighWater;result.maxBatchMilliseconds=state.maxBatchMilliseconds;result.timings=state.timings;result.decodedValues=state.decodedValues;result.invalidValues=state.invalidValues;result.sourceCounts=state.sourceCounts;result.channelLost=state.channelLost;result.channelOverflows=state.channelOverflows;result.devices=state.devices;result.clocks=state.clocks;result.rtActivity=state.rtActivity;result.hardware=state.hardware;result.failed=state.failed;result.error=state.error;result.checkCount=state.checkCount;return result;
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
}
