#include "acquisition.hpp"
#include <iostream>
#include <stdexcept>

namespace {
struct Observations {
  std::thread::id owner;
  std::atomic<unsigned> opens{},starts{},polls{},stops{},closes{},destroyed{},mismatches{};
  bool failClose{};
};
class AffineSource final : public aero::CaptureSource {
  std::shared_ptr<Observations> observed;
  bool draining{};
  void check(){const auto now=std::this_thread::get_id();if(observed->owner==std::thread::id{})observed->owner=now;else if(observed->owner!=now)++observed->mismatches;}
public:
  explicit AffineSource(std::shared_ptr<Observations> value):observed(std::move(value)){}
  ~AffineSource() override {check();++observed->destroyed;}
  std::string open(const aero::CaptureConfig &) override {check();++observed->opens;return {};}
  void updateCallbacks(const aero::CaptureConfig &) override {check();}
  std::string startMonitoring() override {check();++observed->starts;return {};}
  std::string poll(aero::SourceBatch &,std::stop_token) override {check();++observed->polls;std::this_thread::sleep_for(std::chrono::milliseconds(1));return {};}
  std::string stopMonitoring() override {check();++observed->stops;draining=true;return {};}
  std::string drainNext(aero::SourceBatch &out,bool &more) override {check();more=false;if(draining){aero::Record r;r.id=observed->stops;r.channel=1;r.words={0x1234};out.records.push_back(r);draining=false;}return {};}
  void close() override {check();++observed->closes;}
  std::string closeError() const override {return observed->failClose?"Fixture driver handle was not released":"";}
};
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
}
int main(){
  const auto directory=std::filesystem::temp_directory_path()/("aerotrace-owner-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directory(directory);
    auto observed=std::make_shared<Observations>();
    aero::CaptureConfig config;config.enabled={true,false,false,false};
    {
      aero::Acquisition acquisition;
      require(acquisition.connectConfigured(config,std::make_unique<AffineSource>(observed)),"connection failed");
      require(acquisition.connected()&&!acquisition.active()&&observed->starts==0&&observed->polls==0,"Connect received before explicit Start");
      for(unsigned i=0;i<20;++i){
        const auto file=directory/(std::to_string(i)+".atd");
        require(acquisition.startConfigured(file,config),"retained connection start failed");
        acquisition.stopRecording();
        const auto recording=aero::read(file);
        require(acquisition.connected()&&!acquisition.error(),"stop lost healthy connection");
        require(recording.error.empty()&&!recording.recovered&&recording.records.size()==1,"incremental drain was not preserved exactly");
      }
      acquisition.stop();
    }
    require(observed->opens==1&&observed->starts==20&&observed->stops==20,"retention did not preserve one connection");
    require(observed->mismatches==0&&observed->destroyed==1,"SDK lifecycle/destruction moved to another thread");
    auto broken=std::make_shared<Observations>();broken->failClose=true;
    {
      aero::Acquisition acquisition;
      require(acquisition.connectConfigured(config,std::make_unique<AffineSource>(broken)),"cleanup fixture connection failed");
      acquisition.stop();
      require(acquisition.metrics(false).cleanupBlocked,"cleanup failure was not retained");
      require(!acquisition.connectConfigured(config,std::make_unique<AffineSource>(std::make_shared<Observations>())),"new backend admitted after cleanup failure");
      require(acquisition.metrics(false).cleanupBlocked,"failed reconnect erased cleanup evidence");
    }
    std::filesystem::remove_all(directory);
    std::cout<<"SDK connection ownership, retained Start/Stop, exact drain and cleanup barrier passed\n";
    return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
