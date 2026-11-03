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
    std::filesystem::create_directory(root);
    aero::Acquisition acquisition;aero::CaptureConfig config;config.hardware=false;config.displayCapacity=64;
    CHECK(acquisition.setProfile({aero::Signal{"TEMP","degC"}}).empty());
    const auto began=steady_clock::now();
    CHECK(acquisition.startConfigured(root/"burst.atd",config,std::make_unique<Burst>()));
    const auto deadline=steady_clock::now()+seconds(15);
    while(acquisition.metrics().recorded<100000 && acquisition.active() && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(1));
    acquisition.stop();const auto elapsed=duration<double>(steady_clock::now()-began).count();
    const auto metrics=acquisition.metrics();CHECK(!metrics.failed && metrics.recorded==100000);
    CHECK(metrics.queueDepth==64 && metrics.queueHighWater==64 && metrics.displayDrops==99936);
    auto data=aero::read(root/"burst.atd");CHECK(data.error.empty() && !data.recovered && data.records.size()==100000);
    for(unsigned index=0;index<100000;++index) CHECK(data.records[index].id==index+1);
    const auto latest=acquisition.liveFrame();CHECK(latest && latest->records==100000 && latest->traces[0].size()<=1024);
    const auto display=acquisition.drain(16);CHECK(display.size()==16 && acquisition.metrics().queueDepth==48);
    std::cout<<"PASS: 100000 raw records retained with stalled display; 64-record queue; 99936 display skips\n"
      <<"Measured pipeline events/s="<<100000/elapsed<<", seconds="<<elapsed<<", max_batch_ms="<<metrics.maxBatchMilliseconds<<'\n';
    std::filesystem::remove_all(root);
  } catch(const std::exception &error) {std::cerr<<error.what()<<"\nFixtures: "<<root<<'\n';return 1;}
}
