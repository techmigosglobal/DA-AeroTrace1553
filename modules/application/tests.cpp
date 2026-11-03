#include "workbench.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

#define CHECK(value) do { if(!(value)) throw std::runtime_error(#value); } while(false)
int main() {
  try {
    {
      aero::Workbench w;w.captureConfig.hardware=false;
      aero::Signal route{"RT_TX","u"};route.rt=4;route.sa=7;route.transmit=true;
      CHECK(w.applyProfile({route}).empty());aero::Record r;r.id=1;r.source=1;r.validity=15;r.messageType=2;
      r.commandWord=(3<<11)|(2<<5)|1;r.commandWord2=(4<<11)|(1<<10)|(7<<5)|1;r.statusWord=4<<11;r.statusWord2=3<<11;r.words={1234};
      w.archive={r};CHECK(w.latest(route,{})==12.34 && w.liveValue(0,{}).value==12.34);
      CHECK(w.series("raw_column",0,{},0,-1,4).primary[0].second==4 && w.series("raw_column",0,{},0,-1,5).primary[0].second==7);
      CHECK(w.series("raw_column",0,{},0,-1,6).primary.empty());
      aero::LiveDecoder decoder;decoder.configure({route},1);decoder.consume(w.archive);CHECK(decoder.snapshot().values[0].value==12.34);
      const auto exported=aero::Workbench::exportProfileRecords(w.archive,{route},aero::ExportFormat::EngineeringCsv,0,UINT64_MAX);
      CHECK(exported.error.empty() && exported.count==1 && exported.text.find(",12.34,")!=std::string::npos);
      r.id=2;r.statusWord|=8;w.archive.push_back(r);CHECK(!w.latest(route,{}) && !w.liveValue(0,{}).value);
      decoder.consume(std::span<const aero::Record>(&r,1));CHECK(!decoder.snapshot().values[0].value && decoder.snapshot().values[0].reason.find("busy")!=std::string_view::npos);
    }
    {
      aero::Workbench isolated;isolated.captureConfig.hardware=false;isolated.applyProfile(aero::Workbench::demoSignals());
      auto first=aero::synthetic(0);first.timestampNs=1234567890;auto second=first;second.id=2;second.channel=2;second.bus=1;second.timestampNs=999999999000;second.words[0]=2000;
      isolated.archive={first,second};
      CHECK(isolated.liveValue(0,{},1,0).value==10 && isolated.liveValue(0,{},2,1).value==20);
      CHECK(isolated.liveValue(0,{},1,0).timestampNs==first.timestampNs);
      CHECK(isolated.series("",0,{},1,0).primary.size()==1);
      CHECK(isolated.series("raw_column",0,{},2,1,17).primary[0].second==2000);
      auto later=second;later.timestampNs+=1234000;isolated.archive.push_back(later);
      CHECK(isolated.series("raw_column",0,{},2,1,11).primary[0].second==1234);
      isolated.archive.pop_back();
      aero::LiveDecoder decoder;decoder.configure(isolated.mappings(),1);decoder.consume(isolated.archive);
      auto frame=decoder.snapshot();CHECK(frame.sourceTraces[0][0].size()==1 && frame.sourceTraces[3][0].size()==1);
      first.id=3;first.timestampNs=100;decoder.consume(std::span<const aero::Record>(&first,1));frame=decoder.snapshot();
      CHECK(frame.sourceTraces[0][0].size()==1 && frame.sourceTraces[0][0][0].first==1e-7);
      auto many=isolated.mappings();for(unsigned i=many.size();i<512;++i) {auto field=many[0];field.id="FIELD_"+std::to_string(i);many.push_back(field);}
      decoder.configure(many,2);decoder.consume(std::span<const aero::Record>(&second,1));decoder.selectTraces({511});frame=decoder.snapshot();
      CHECK(frame.traceSignals==std::vector<unsigned>{511} && frame.sourceTraces[3][0].size()==1 && frame.sourceTraces[3][0][0].second==20);
      second.timestampNs+=1000000;decoder.consume(std::span<const aero::Record>(&second,1));frame=decoder.snapshot();
      CHECK(frame.sourceTraces[3][0].size()==2 && frame.traces.size()==1);
    }
    aero::Workbench workbench;
    workbench.captureConfig.hardware=false;
    workbench.applyProfile(aero::Workbench::demoSignals());
    for(unsigned i=0;i<12;++i) workbench.archive.push_back(aero::synthetic(i));
    const auto captured=workbench.archive;
    workbench.replayRecords=captured;workbench.replaySource=true;
    CHECK(workbench.seek(2)); CHECK(workbench.selected==captured[2]);
    workbench.replayPlaying=true;workbench.replaySpeed=2;
    workbench.advance(83); CHECK(workbench.replayIndex==4);
    workbench.advance(10000);CHECK(!workbench.replayPlaying);
    CHECK(workbench.seek(9999));CHECK(workbench.replayIndex==11);
    CHECK(workbench.end({})==12);
    auto csv=workbench.exportData(aero::ExportFormat::RawCsv,0,86399999);
    CHECK(csv.error.empty() && csv.count==12);
    CHECK(csv.text.find("SIMULATED")!=std::string::npos);
    workbench.signal.id="TEMP,\"quoted\"\nline";
    auto decoded=workbench.exportData(aero::ExportFormat::EngineeringCsv,0,0);
    CHECK(decoded.count==1); CHECK(decoded.text.find("\"TEMP,\"\"quoted\"\"\nline\"")!=std::string::npos);
    auto json=workbench.exportData(aero::ExportFormat::RawJson,0,0);
    CHECK(json.count==1);CHECK(json.text.find("TEMP,\\\"quoted\\\"\\nline")!=std::string::npos);
    CHECK(!workbench.exportData(aero::ExportFormat::RawCsv,10,1).error.empty());
    CHECK(workbench.series("distribution",0,{}).primary.size()==32);
    CHECK(!workbench.series("rate",0,{}).primary.empty());
    CHECK(!workbench.series("",0,{}).primary.empty());
    CHECK(workbench.latest(workbench.signal,{}).has_value());
    workbench.replayRecords.clear(); CHECK(!workbench.seek(0));
    CHECK(workbench.data({}).empty());
    workbench.replaySource=false; CHECK(workbench.data({})==captured);
    CHECK(workbench.series("replay",0,{}).primary.empty());
    for(const auto &point:workbench.series("events",0,{}).primary) CHECK(point.second==0);
    std::cout<<"PASS: Qt-free replay, analysis, export ranges and escaping\n";
  } catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 1; }
}
