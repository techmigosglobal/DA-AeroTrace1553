#include "acquisition.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x)) throw std::runtime_error(#x);}while(false)
namespace {
class Burst final : public aero::CaptureSource {
  unsigned next{};
public:
  std::string open(const aero::CaptureConfig &) override {next=0;return {};}
  std::string poll(aero::SourceBatch &batch,std::stop_token) override {
    for(unsigned index=0;index<512 && next<100000;++index,++next) {
      auto record=aero::synthetic(next);record.timestampNs=next*20000;batch.records.push_back(std::move(record));
    }
    if(batch.records.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(1));return {};
  }
  void close() override {}
};
}
int main() {
  using namespace std::chrono;
  const auto root=std::filesystem::temp_directory_path()/("aerotrace-pipeline-"+std::to_string(steady_clock::now().time_since_epoch().count()));
  try {
    aero::Signal mapping{"TEMP","degC"};auto record=aero::synthetic(0);
    mapping.width=32;mapping.scale=1;record.words={0xFFFF,0xFFFE};record.commandWord=(record.commandWord&~31)|2;mapping.signedValue=true;
    CHECK(aero::decode(record,mapping).value()==-2);
    mapping.signedValue=false;mapping.numericType="float32";record.words={0x4148,0};CHECK(aero::decode(record,mapping).value()==12.5);
    mapping.leastSignificantWordFirst=true;record.words={0,0x4148};CHECK(aero::decode(record,mapping).value()==12.5);
    mapping.leastSignificantWordFirst=false;mapping.numericType="bcd";mapping.width=16;record.words={0x1234};record.commandWord=(record.commandWord&~31)|1;CHECK(aero::decode(record,mapping).value()==1234);
    record.words={0x12FA};CHECK(!aero::decode(record,mapping));
    mapping.numericType="float64";mapping.width=64;record.words={0x7FF0,0,0,0};record.commandWord=(record.commandWord&~31)|4;CHECK(!aero::decode(record,mapping));
    std::vector<aero::Sample> samples;for(unsigned index=0;index<100000;++index) samples.emplace_back(index,index==50001 ? 99999 : 0);
    const auto reduced=aero::downsample(samples,512);CHECK(reduced.size()<=512 && reduced.front()==samples.front() && reduced.back()==samples.back());
    CHECK(std::any_of(reduced.begin(),reduced.end(),[](auto point){return point.second==99999;}));
    CHECK(std::is_sorted(reduced.begin(),reduced.end()));
    {
      aero::CommandTrace trace;
      for(unsigned bucket=0;bucket<650;++bucket) {
        const auto ns=std::uint64_t(bucket)*100000000;
        trace.append(ns,1.);
        trace.append(ns+50000000,10.);
      }
      const auto chart=trace.points();
      CHECK(chart.size()==1200 && chart.front().first==5 && chart.back().second==10);
      trace.append(65000000000,std::nullopt);
      trace.append(65050000000,3.);
      const auto invalid=trace.points();
      CHECK(std::isnan(invalid[invalid.size()-2].second) && invalid.back().second==3);
      trace.append(20000000,7.);
      const auto reset=trace.points();
      CHECK(reset.size()==2 && std::isnan(reset.front().second) && reset.back().second==7);
    }
    {
      std::vector<aero::Signal> fields;
      for(unsigned i=0;i<512;++i) {
        auto field=aero::Signal{"manual/test/"+std::to_string(i),""};
        field.icdSetId="manual";field.messageId="test";field.expectedWordCount=32;
        field.word=i/16;field.lsb=i%16;field.width=1;fields.push_back(std::move(field));
      }
      CHECK(aero::validateProfile(fields).empty());
      aero::LiveDecoder decoder;
      decoder.configure(fields,1);
      decoder.selectCommandPlot({"test",1,0,0,8});
      auto sample=aero::synthetic(0);sample.commandWord&=~31;sample.words.resize(32);
      decoder.consume(std::span{&sample,1});
      decoder.selectCommandPlot({"test",1,0,511,1});
      const auto frame=decoder.snapshot();
      CHECK(frame.commandPlot && frame.commandPlot->traces.size()==1 && !frame.commandPlot->traces.front().empty());
      fields.push_back(fields.back());
      CHECK(!aero::validateProfile(fields).empty());
    }
    std::filesystem::create_directory(root);
    aero::Acquisition acquisition;aero::CaptureConfig config;config.hardware=false;config.displayCapacity=64;
    CHECK(acquisition.setProfile({aero::Signal{"TEMP","degC"}}).empty());
    const auto began=steady_clock::now();
    CHECK(acquisition.startConfigured(root/"burst.atd",config,std::make_unique<Burst>()));
#ifdef AEROTRACE_SANITIZED_TEST
    const auto deadline=steady_clock::now()+seconds(120);
#else
    const auto deadline=steady_clock::now()+seconds(15);
#endif
    while(acquisition.metrics().recorded<100000 && acquisition.active() && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(1));
    acquisition.stop();const auto elapsed=duration<double>(steady_clock::now()-began).count();
    const auto metrics=acquisition.metrics();CHECK(!metrics.failed && metrics.recorded==100000);
    CHECK(metrics.checkpointed==100000 && !metrics.writerRejected && metrics.writerQueueHighWater>0);
    CHECK(metrics.queueDepth==64 && metrics.queueHighWater==64 && metrics.displayDrops==99936);
    auto data=aero::read(root/"burst.atd");CHECK(data.error.empty() && !data.recovered && data.records.size()==100000);
    for(unsigned index=0;index<100000;++index) CHECK(data.records[index].id==index+1);
    const auto latest=acquisition.liveFrame();CHECK(latest && latest->records==100000 && latest->traces[0].size()<=1024);
    const auto display=acquisition.drain(16);CHECK(display.size()==16 && acquisition.metrics().queueDepth==48);
    CHECK(!acquisition.startConfigured(root/"burst.atd",config,std::make_unique<Burst>()));
    CHECK(aero::scanRecording(root/"burst.atd",[](const auto &,auto){return true;}).count==100000);
    config.writerCapacityBytes=1024; // One fixture batch cannot fit; no later batch may bypass it.
    CHECK(acquisition.startConfigured(root/"saturated.atd",config,std::make_unique<Burst>()));
    while(acquisition.active() && steady_clock::now()<deadline)std::this_thread::sleep_for(milliseconds(1));
    acquisition.stop();
    const auto saturated=acquisition.metrics();
    CHECK(saturated.failed && saturated.writerRejected>0 && saturated.recorded==0);
    CHECK(saturated.error.find("queue saturated")!=std::string::npos);
    const auto prefix=aero::scanRecording(root/"saturated.atd",[](const auto &,auto){return true;});
    CHECK(prefix.count==0 && prefix.error.empty());
    std::cout<<"PASS: 100000 raw records retained with stalled display; 64-record queue; 99936 display skips\n"
      <<"Measured pipeline events/s="<<100000/elapsed<<", seconds="<<elapsed<<", max_batch_ms="<<metrics.maxBatchMilliseconds<<'\n';
    std::filesystem::remove_all(root);
  } catch(const std::exception &error) {std::cerr<<error.what()<<"\nFixtures: "<<root<<'\n';return 1;}
}
