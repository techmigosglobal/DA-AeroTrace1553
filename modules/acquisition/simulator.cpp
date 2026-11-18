#include "source.hpp"
#include "example_simulation.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace aero {
std::string validateSimulation(const CaptureConfig &config) {
  if(config.simulatorRate<1 || config.simulatorRate>100000)
    return "Simulated message rate must be 1–100000 messages/s";
  if(std::none_of(config.enabled.begin(),config.enabled.end(),[](bool enabled){return enabled;}))
    return "Enable at least one simulation stream";
  return {};
}

Record simulationRecord(const CaptureConfig &config,std::uint64_t index) {
  std::array<unsigned,4> streams{};
  unsigned count=0;
  for(unsigned stream=0;stream<config.enabled.size();++stream)
    if(config.enabled[stream]) streams[count++]=stream;
  if(!count) throw std::invalid_argument("Enable at least one simulation stream");
  auto record=exampleSimulationRecord(index,config.simulatorRate,streams[index%count]);
  if((index/count)%2) {
    record.commandWord=(8u<<11)|(12u<<5);
    record.statusWord=8u<<11;
    const auto raw=std::uint32_t(2000+(index/(2*count))%400);
    record.words[0]=std::uint16_t(raw);
    record.words[1]=0;
  }
  return record;
}

namespace {
class Simulator final : public CaptureSource {
  CaptureConfig config;
  std::chrono::steady_clock::time_point start;
  std::uint64_t next{};
public:
  std::string open(const CaptureConfig &value) override {
    config=value;start=std::chrono::steady_clock::now();next=0;
    return validateSimulation(config);
  }
  std::string poll(SourceBatch &batch,std::stop_token stop) override {
    using namespace std::chrono;
    const auto elapsed=duration_cast<nanoseconds>(steady_clock::now()-start).count();
    const auto target=std::uint64_t(elapsed/1000000000)*config.simulatorRate+std::uint64_t(elapsed%1000000000)*config.simulatorRate/1000000000+1;
    for(unsigned count=0;next<target && count<1024 && !stop.stop_requested();++count) {
      batch.records.push_back(simulationRecord(config,next++));
    }
    if(batch.records.empty()) std::this_thread::sleep_for(milliseconds(1));
    return {};
  }
  void close() override {}
};
}
std::unique_ptr<CaptureSource> makeSimulator() { return std::make_unique<Simulator>(); }
}
