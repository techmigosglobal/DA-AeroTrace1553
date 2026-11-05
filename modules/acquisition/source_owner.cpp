#include "source.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace aero {
namespace {
// The source may be transferred between connection/recording coordinators, but
// its vendor calls and destruction always remain on this connection's thread.
class SdkOwnedSource final : public CaptureSource {
  std::unique_ptr<CaptureSource> source;
  mutable std::mutex mutex;
  mutable std::condition_variable changed;
  mutable std::deque<std::function<void()>> commands;
  bool stopping{};
  std::jthread owner;

  template<class F> auto invoke(F function) const -> decltype(function(*source)) {
    using Result=decltype(function(*source));
    if(std::this_thread::get_id()==owner.get_id())return function(*source);
    auto task=std::make_shared<std::packaged_task<Result()>>([this,function=std::move(function)]() mutable {return function(*source);});
    auto result=task->get_future();
    {
      std::lock_guard lock(mutex);
      if(stopping)throw std::runtime_error("SDK owner is closing");
      if(commands.size()>=64)throw std::runtime_error("SDK command queue is full");
      commands.emplace_back([task]{(*task)();});
    }
    changed.notify_one();
    return result.get();
  }
public:
  explicit SdkOwnedSource(std::unique_ptr<CaptureSource> value):source(std::move(value)),owner([this]{
    for(;;){
      std::function<void()> command;
      {
        std::unique_lock lock(mutex);
        changed.wait(lock,[this]{return stopping||!commands.empty();});
        if(commands.empty()&&stopping)break;
        command=std::move(commands.front());commands.pop_front();
      }
      command();
    }
    source.reset();
  }){}
  ~SdkOwnedSource() override {
    // Closing is explicit in Acquisition. Destruction of a vendor source may
    // also release resources, so destroy it before its owner thread exits.
    {std::lock_guard lock(mutex);stopping=true;}
    changed.notify_all();
    if(owner.joinable())owner.join();
  }
  std::string open(const CaptureConfig &c) override {return invoke([&](CaptureSource &s){return s.open(c);});}
  void updateCallbacks(const CaptureConfig &c) override {invoke([&](CaptureSource &s){s.updateCallbacks(c);});}
  std::string startMonitoring() override {return invoke([](CaptureSource &s){return s.startMonitoring();});}
  std::string poll(SourceBatch &b,std::stop_token t) override {return invoke([&](CaptureSource &s){return s.poll(b,t);});}
  std::string finish(SourceBatch &b) override {return invoke([&](CaptureSource &s){return s.finish(b);});}
  std::string stopMonitoring() override {return invoke([](CaptureSource &s){return s.stopMonitoring();});}
  std::string drainNext(SourceBatch &b,bool &more) override {return invoke([&](CaptureSource &s){return s.drainNext(b,more);});}
  void close() override {invoke([](CaptureSource &s){s.close();});}
  std::string closeError() const override {return invoke([](CaptureSource &s){return s.closeError();});}
  CaptureFailure lastFailure() const override {return invoke([](CaptureSource &s){return s.lastFailure();});}
  CaptureIdentity identity() const override {return invoke([](CaptureSource &s){return s.identity();});}
};
}
std::unique_ptr<CaptureSource> makeSdkOwnedSource(std::unique_ptr<CaptureSource> source){
  if(!source)throw std::invalid_argument("SDK source is missing");
  if(dynamic_cast<SdkOwnedSource *>(source.get()))return source;
  return std::make_unique<SdkOwnedSource>(std::move(source));
}
}
