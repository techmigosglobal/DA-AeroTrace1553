#include "source.hpp"
#include "live_decoder.hpp"
#include "signal_checks.hpp"
#include "example_simulation.hpp"
#include "protocol.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
  {const auto profile=aero::exampleSimulationProfile();CHECK(profile.size()==66);auto demo=aero::exampleSimulationRecord(0,10,0);CHECK(demo.words.size()==32&&demo.commandWord==((3<<11)|(12<<5))&&demo.words[30]==0&&demo.words[31]==0);CHECK(!aero::decode(demo,profile[0]));CHECK(std::abs(aero::decode(demo,profile[0],true).value_or(-1)-100)<.001);CHECK(std::abs(aero::decode(demo,profile[6],true).value_or(-1)-1)<.001);auto different=demo;different.commandWord|=31;CHECK(!aero::decode(different,profile[0],true));
   auto bit=profile[15];bit.width=1;bit.lsb=0;bit.unit="state";auto tick=aero::exampleSimulationRecord(10,10,0);CHECK(*aero::decode(tick,bit,true)==1);bit.lsb=15;CHECK(*aero::decode(tick,bit,true)==1);CHECK(tick.words[27]==0xffff);tick.source=1;tick.validity=5;tick.messageType=0;CHECK(aero::interpret(tick).valid);CHECK(!aero::decode(tick,bit,true));}
  aero::Signal s{"TEMP","degC"};s.scale=.1;s.channel=1;s.bus=0;
  const auto profile=aero::exampleSimulationProfile();
  for(unsigned mask=1;mask<16;++mask) {
    aero::CaptureConfig config;config.hardware=false;
    for(unsigned k=0;k<4;++k) config.enabled[k]=(mask & (1u<<k))!=0;
    CHECK(aero::validateSimulation(config).empty());
    unsigned observed[2]{};
    const auto streams=std::count(config.enabled.begin(),config.enabled.end(),true);
    for(unsigned index=0;index<2*streams;++index) {
      const auto packet=aero::simulationRecord(config,index);
      const auto route=aero::command(packet.commandWord);
      CHECK(packet.words.size()==32 && route.count==32 && route.sa==12 && !route.transmit);
      CHECK(route.rt==3 || route.rt==8);
      const unsigned stream=(packet.channel-1)*2+packet.bus;
      CHECK(config.enabled[stream]);observed[route.rt==8]|=1u<<stream;
      if(route.rt==3) {
        CHECK(packet==aero::exampleSimulationRecord(index,config.simulatorRate,stream));
        CHECK(aero::decode(packet,profile.front(),true).has_value());
      } else {
        auto rt8=profile.front();rt8.rt=8;rt8.numericType="integer";rt8.leastSignificantWordFirst=true;
        rt8.scale=.25;rt8.offset=-10;
        CHECK(packet.words[0]==2000 && packet.words[1]==0);
        CHECK(aero::decode(packet,rt8,true)==490);
        CHECK(!aero::decodeValue(packet,profile.front(),true).matched);
      }
    }
    CHECK(observed[0]==mask && observed[1]==mask);
  }
  auto hardware=aero::exampleSimulationRecord(0,12,0);
  hardware.source=1;hardware.validity=5;hardware.messageType=0;
  CHECK(aero::interpret(hardware).valid);
  CHECK(!aero::decode(hardware,profile.front(),true));
  auto real=profile.front();real.simulationOnly=false;
  CHECK(aero::decode(hardware,real)==100);
  aero::LiveDecoder live;live.configure({real},1);
  const auto testWords=std::array<std::uint16_t,3>{0x42b4,0x42c8,0x42dc};
  for(unsigned i=0;i<testWords.size();++i) {
    hardware.id=i+1;hardware.timestampNs=std::uint64_t(i)*1000000;
    hardware.words[0]=testWords[i];
    live.consume(std::span<const aero::Record>(&hardware,1));
    CHECK(live.snapshot().sourceValues[0][0].value==90+10*i);
  }
  const auto engineering=live.snapshot();
  CHECK(engineering.sourceTraces[0][0].size()==3);
  for(unsigned i=0;i<testWords.size();++i)
    CHECK(engineering.sourceTraces[0][0][i].second==90+10*i);

  auto countMismatch=hardware;countMismatch.commandWord|=31;countMismatch.words.resize(31);
  CHECK(aero::decodeValue(countMismatch,real).reason=="Expected word count mismatch");
  auto differentStream=real;differentStream.channel=2;
  CHECK(!aero::decodeValue(hardware,differentStream).matched);
  aero::SignalChecks checks;aero::CheckRule rule{"TEMP",10.,30.,100000000,1};checks.configure({s},{rule});
  auto record=[&](std::uint64_t ms,unsigned raw){aero::Record v;v.id=ms+1;v.timestampNs=ms*1000000;v.commandWord=(3<<11)|(2<<5)|1;v.words={std::uint16_t(raw)};return v;};
  checks.consume(record(0,200));CHECK(checks.drain().empty());checks.consume(record(20,400));CHECK(checks.drain().at(0).kind=="range");
  checks.consume(record(30,305));CHECK(checks.drain().empty());checks.consume(record(40,290));CHECK(checks.drain().at(0).kind=="range_recovered");
  checks.advance(0,141000000);CHECK(checks.drain().at(0).kind=="missing");checks.advance(0,200000000);CHECK(checks.drain().empty());checks.consume(record(201,200));CHECK(checks.drain().at(0).kind=="recovered");
  checks.consume(record(500,200));auto gap=checks.drain();CHECK(gap.size()==2 && gap[0].kind=="missing" && gap[1].kind=="recovered");
  checks.consume(record(1,200));CHECK(checks.drain().at(0).kind=="clock_reset");
  auto bad=record(2,200);bad.flags=1;checks.consume(bad);CHECK(checks.drain().at(0).kind=="protocol");checks.consume(bad);CHECK(checks.drain().empty());
  checks.configure({s},{rule});auto other=record(0,200);other.commandWord=(4<<11)|(2<<5)|1;checks.consume(other);checks.drain();checks.advance(0,101000000);CHECK(checks.drain().at(0).kind=="missing");
  auto wrong=rule;wrong.signalId="UNKNOWN";CHECK(!aero::validateChecks({wrong},{s}).empty());wrong=rule;wrong.minimum=50;CHECK(!aero::validateChecks({wrong},{s}).empty());
  std::cout<<"PASS: RT 03 and RT 08 fixture, real-profile matching, provenance and word counts, missing gaps, hysteresis, clock resets and protocol checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
