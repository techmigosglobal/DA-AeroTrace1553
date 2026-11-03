#include "acquisition.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <condition_variable>

struct OpenGate {std::mutex mutex;std::condition_variable changed;bool entered{},released{};};
class GatedOpen final : public aero::CaptureSource {
  std::shared_ptr<OpenGate> gate;
public:
  explicit GatedOpen(std::shared_ptr<OpenGate> value):gate(std::move(value)){}
  std::string open(const aero::CaptureConfig &) override {std::unique_lock lock(gate->mutex);gate->entered=true;gate->changed.notify_all();gate->changed.wait(lock,[&]{return gate->released;});return {};}
  std::string poll(aero::SourceBatch &,std::stop_token) override {std::this_thread::sleep_for(std::chrono::milliseconds(1));return {};}
  void close() override {}
};

class SlowOpen final : public aero::CaptureSource {
public:
  std::string open(const aero::CaptureConfig &config) override {
    for(int i=0;i<30;++i) {
      if(config.stopToken.stop_requested()) return "Cancelled";
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {};
  }
  std::string poll(aero::SourceBatch &,std::stop_token) override {std::this_thread::sleep_for(std::chrono::milliseconds(1));return {};}
  void close() override {}
};

struct LifecycleCounts {std::atomic<unsigned> opens{},starts{},polls{},finishes{},closes{};bool failOpen{},failPoll{};};
class LifecycleSource final : public aero::CaptureSource {
  std::shared_ptr<LifecycleCounts> counts;
public:
  explicit LifecycleSource(std::shared_ptr<LifecycleCounts> value):counts(std::move(value)){}
  std::string open(const aero::CaptureConfig &) override {++counts->opens;return counts->failOpen?"Open failed":std::string();}
  std::string startMonitoring() override {++counts->starts;return {};}
  std::string poll(aero::SourceBatch &batch,std::stop_token) override {
    ++counts->polls;if(counts->failPoll)return "Packet read failed";
    aero::Record record;record.id=counts->polls;record.channel=1;record.words={0x1234};batch.records.push_back(record);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));return {};
  }
  std::string finish(aero::SourceBatch &) override {++counts->finishes;return {};}
  void close() override {++counts->closes;}
};

int main() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("aerotrace-acquisition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directory(directory);
    aero::Acquisition acquisition;
    const auto path = directory / "capture.atd";
    if (!acquisition.start(path, {true, false, false, false})) throw std::runtime_error("start failed");
    std::vector<aero::Record> delivered;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (delivered.size() < 2 && std::chrono::steady_clock::now() < deadline) {
      auto batch = acquisition.drain();
      delivered.insert(delivered.end(), batch.begin(), batch.end());
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    acquisition.stop();
    auto tail = acquisition.drain();
    delivered.insert(delivered.end(), tail.begin(), tail.end());
    auto recording = aero::read(path);
    if (delivered.size() < 2 || acquisition.active() || acquisition.error() ||
        recording.recovered || !recording.error.empty() || recording.records != delivered)
      throw std::runtime_error("record/drain/stop contract failed");
    for (const auto &record : delivered)
      if (record.channel != 1 || record.bus != 0) throw std::runtime_error("disabled source recorded");
    if (acquisition.start(directory / "missing" / "capture.atd")) throw std::runtime_error("invalid path accepted");
    if (!acquisition.start(directory / "restart.atd")) throw std::runtime_error("restart failed");
    acquisition.stop();
    aero::CaptureConfig delayed;delayed.hardware=false;
    auto gate=std::make_shared<OpenGate>();
    auto returned=std::async(std::launch::async,[&]{return acquisition.beginConfigured(directory/"delayed.atd",delayed,std::make_unique<GatedOpen>(gate));});
    bool entered=false;{std::unique_lock lock(gate->mutex);entered=gate->changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate->entered;});}
    const bool returnedWhileOpenBlocked=returned.wait_for(std::chrono::seconds(5))==std::future_status::ready;
    {std::lock_guard lock(gate->mutex);gate->released=true;}gate->changed.notify_all();
    auto initialized=returned.get();
    if(!entered||!returnedWhileOpenBlocked)throw std::runtime_error("Device initialization did not return while open was blocked");
    if(!initialized.get() || !acquisition.active()) throw std::runtime_error("Asynchronous initialization did not complete");
    acquisition.stop();
    auto cancelled=acquisition.beginConfigured(directory/"cancelled.atd",delayed,std::make_unique<SlowOpen>());
    acquisition.requestStop();if(cancelled.get()) throw std::runtime_error("Cancelled initialization started capture");acquisition.stop();
    // Connection opens/configures only: no SDK start, poll or recording files.
    auto lifecycle=std::make_shared<LifecycleCounts>();aero::CaptureConfig hardware;
    const auto filesBefore=std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{});
    if(!acquisition.connectConfigured(hardware,std::make_unique<LifecycleSource>(lifecycle)) || !acquisition.connected() || acquisition.active())throw std::runtime_error("Connection state contract failed");
    if(lifecycle->opens!=1 || lifecycle->starts || lifecycle->polls || filesBefore!=std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{}))throw std::runtime_error("Connecting started acquisition or created recording files");
    for(unsigned run=0;run<3;++run) {
      if(!acquisition.startConfigured(directory/("lifecycle"+std::to_string(run)+".atd"),hardware))throw std::runtime_error("Recording on connected source failed");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));acquisition.stopRecording();
      if(!acquisition.connected() || acquisition.active() || acquisition.metrics().failed || acquisition.metrics().recorded==0)throw std::runtime_error("Stop recording did not retain the connection");
    }
    if(lifecycle->opens!=1 || lifecycle->starts!=3 || lifecycle->finishes!=3 || lifecycle->closes!=0)throw std::runtime_error("Repeated recording reopened or closed the connected source");
    acquisition.stop();if(acquisition.connected() || lifecycle->closes!=1)throw std::runtime_error("Disconnect did not release connection");
    lifecycle=std::make_shared<LifecycleCounts>();lifecycle->failOpen=true;
    if(acquisition.connectConfigured(hardware,std::make_unique<LifecycleSource>(lifecycle)) || acquisition.connected())throw std::runtime_error("Failed connection marked connected");
    lifecycle=std::make_shared<LifecycleCounts>();lifecycle->failPoll=true;
    if(!acquisition.connectConfigured(hardware,std::make_unique<LifecycleSource>(lifecycle)))throw std::runtime_error("Fault fixture connect failed");
    if(!acquisition.startConfigured(directory/"lifecycle-error.atd",hardware))throw std::runtime_error("Fault fixture record failed");
    const auto faultDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!acquisition.error() && std::chrono::steady_clock::now()<faultDeadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    acquisition.stopRecording();if(acquisition.connected() || !acquisition.error())throw std::runtime_error("SDK failure retained a stale connection");
    auto cancelledConnection=acquisition.beginConnection(delayed,std::make_unique<SlowOpen>());
    acquisition.requestStop();if(cancelledConnection.get())throw std::runtime_error("Cancelled connection became ready");acquisition.stop();
    if(acquisition.connected())throw std::runtime_error("Cancelled connection retained handles");
    std::filesystem::remove(path);
    std::filesystem::remove(directory / "restart.atd");
    std::filesystem::remove_all(directory);
    std::cout << "PASS: Qt-free acquisition, filtering, exact recording, restart\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\nFixtures: " << directory << '\n';
    return 1;
  }
}
