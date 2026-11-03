#include "acquisition.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char **argv) {
  const auto info=aero::inspectDdcRuntime();
  std::cout<<info.message<<'\n'<<info.path.string()<<'\n';
  if(!info.available) return 2;
  if(argc>1 && std::string(argv[1])=="--capture-channel") {
    if(argc<5)return 1;
    aero::CaptureConfig config;config.enabled={false,false,false,false};
    const int logical=std::stoi(argv[4]);const unsigned channel=logical==0?0:1;
    config.logicalDevices[channel]=logical;config.enabled[channel*2]=true;
    const unsigned seconds=std::stoul(argv[3]),cycles=argc>5?std::stoul(argv[5]):1;
    aero::Acquisition acquisition;
    if(!acquisition.connectConfigured(config)){std::cerr<<acquisition.metrics().error<<'\n';return 3;}
    std::cout<<"Connected without recording; logical="<<logical<<std::endl;
    for(unsigned i=0;i<cycles;++i){
      auto path=std::filesystem::path(std::string(argv[2])+"-"+std::to_string(i+1)+".atd");
      if(std::filesystem::exists(path)){std::cerr<<"Capture exists";return 5;}
      if(!acquisition.startConfigured(path,config)){std::cerr<<acquisition.metrics().error;return 3;}
      for(unsigned n=0;n<seconds && acquisition.active();++n)std::this_thread::sleep_for(std::chrono::seconds(1));
      acquisition.stopRecording();auto m=acquisition.metrics();
      std::cout<<"cycle="<<i+1<<" records="<<m.recorded<<" lost="<<m.hardwareLost<<" overflow="<<m.hardwareOverflows<<" connected="<<acquisition.connected()<<" error="<<m.error<<std::endl;
      if(m.failed||!acquisition.connected())return 4;
    }
    acquisition.stop();return acquisition.error()?4:0;
  }
  if(argc>1 && (std::string(argv[1])=="--monitor" || std::string(argv[1])=="--capture")) {
    const bool capture=std::string(argv[1])=="--capture";
    aero::CaptureConfig config;
    std::filesystem::path path=std::filesystem::temp_directory_path()/"aerotrace-hardware-probe.atd";
    unsigned seconds=1;
    if(capture) {
      if(argc<3) {std::cerr<<"Usage: aerotrace_hardware_probe --capture <path> [seconds]"<<'\n';return 1;}
      path=argv[2];
      seconds=argc>3 ? std::stoul(argv[3]) : 10;
      config.enabled={true,true,true,true};
    } else if(argc>2) config.logicalDevices[0]=std::stoi(argv[2]);
    aero::Acquisition acquisition;
    if(!acquisition.startConfigured(path,config)) {
      std::cerr<<acquisition.metrics().error<<'\n';return 3;
    }
    std::this_thread::sleep_for(std::chrono::seconds(seconds));acquisition.stop();
    const auto metrics=acquisition.metrics();
    std::cout<<"Received "<<metrics.recorded<<", lost "<<metrics.hardwareLost<<", overflow "<<metrics.hardwareOverflows<<'\n';
    if(metrics.failed) {std::cerr<<metrics.error<<'\n';return 4;}
  }
}
