#include "source.hpp"
#include "signal_checks.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
  aero::Signal s{"TEMP","degC"};s.scale=.1;s.channel=1;s.bus=0;
  aero::CaptureConfig c;c.hardware=false;c.simulatorProfile={s};c.simulatorWave="constant";c.simulatorOffset=20;c.simulatorRate=100;
  CHECK(aero::validateSimulation(c).empty());auto r=aero::simulationRecord(c,0);CHECK(r.source==0 && r.channel==1 && r.bus==0);CHECK(std::abs(*aero::decode(r,s)-20)<1e-9);
  c.simulatorRate=100000;CHECK(aero::simulationRecord(c,36000000000ULL).timestampNs==360000000000000ULL);c.simulatorRate=100;
  auto second=s;second.id="PRESSURE";second.word=1;second.scale=.5;c.simulatorProfile={s,second};CHECK(aero::validateSimulation(c).empty());r=aero::simulationRecord(c,0);CHECK(*aero::decode(r,s)==20 && *aero::decode(r,second)==20);second.word=0;c.simulatorProfile={s,second};CHECK(!aero::validateSimulation(c).empty());c.simulatorProfile={s};
  for(const auto &type:{"integer","float32","float64","bcd"})for(bool lsw:{false,true}){
    auto v=s;v.numericType=type;v.width=type==std::string("float64")?64:type==std::string("float32")?32:16;v.word=3;v.leastSignificantWordFirst=lsw;c.simulatorProfile={v};r=aero::simulationRecord(c,10);CHECK(std::abs(*aero::decode(r,v)-20)<.01);
  }
  c.simulatorProfile={s};c.simulatorWave="noise";auto a=aero::simulationRecord(c,12),b=aero::simulationRecord(c,12);CHECK(a==b);c.simulatorSeed=56;CHECK(a.words!=aero::simulationRecord(c,12).words);
  c.simulatorWave="constant";c.simulatorOffset=70000;r=aero::simulationRecord(c,0);CHECK(*aero::decode(r,s)==6553.5);
  c.simulatorProfile[0].scale=0;CHECK(!aero::validateSimulation(c).empty());c.simulatorProfile={s};c.enabled={false,false,true,true};CHECK(!aero::validateSimulation(c).empty());
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
  std::cout<<"PASS: ICD payload generation, saturation, deterministic noise, missing gaps, hysteresis, clock resets and protocol checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
