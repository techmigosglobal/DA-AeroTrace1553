#include "rt_monitor.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
aero::Record received(unsigned rt=3,unsigned bus=0,unsigned channel=1){
  aero::Record r;r.source=1;r.channel=channel;r.bus=bus;r.validity=5;
  r.commandWord=(rt<<11)|(2<<5)|1;r.statusWord=rt<<11;r.words={123};return r;
}
}
int main(){try{
  aero::RtMonitorFrame frame;frame.session="reference";
  for(unsigned bus=0;bus<2;++bus)for(unsigned i=0;i<632;++i)frame.consume(received(3,bus));
  auto illegal=received(4);illegal.commandWord=(4<<11)|9;illegal.messageType=15;illegal.words.clear();
  for(unsigned i=0;i<40;++i)frame.consume(illegal);
  CHECK(frame.totalCommands[0]==1304&&frame.invalidCommands[0]==40);
  CHECK(frame.channels[0].at(3).buses[0].count==632&&frame.channels[0].at(3).buses[1].count==632);
  CHECK(frame.channels[0].at(4).buses[0].illegalMode==40);
  CHECK(frame.channels[0].at(3).buses[0].noResponseKnown&&frame.channels[0].at(3).buses[0].noResponse==0);

  // The SDK-backed MT and MTI normalizers preserve the documented 0x163f
  // monitor-error mask in Record::flags. Generic Invalid commands must count
  // those records even when their captured command/status/data look valid;
  // this does not manufacture a more specific unsupported error category.
  aero::RtMonitorFrame genericErrors;
  for(unsigned source:{1u,2u})for(std::uint16_t flag:{0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0200,0x0400,0x1000}) {
    auto r=received();r.source=source;r.flags=flag;r.vendorStatus=flag;
    const auto p=aero::interpret(r);CHECK(!p.valid&&aero::protocolIssues(r,p).invalid);genericErrors.consume(r);
  }
  CHECK(genericErrors.totalCommands[0]==18&&genericErrors.invalidCommands[0]==18);
  auto overlapping=received();overlapping.flags=0x163f;overlapping.vendorStatus=0x163f;genericErrors.consume(overlapping);
  CHECK(genericErrors.totalCommands[0]==19&&genericErrors.invalidCommands[0]==19);
  auto markerBits=received();markerBits.vendorStatus=0xe100;
  CHECK(aero::interpret(markerBits).valid&&!aero::protocolIssues(markerBits,aero::interpret(markerBits)).invalid);
  genericErrors.consume(markerBits);CHECK(genericErrors.totalCommands[0]==20&&genericErrors.invalidCommands[0]==19);

  aero::RtMonitorFrame edge;
  auto zero=received(0);CHECK(aero::interpret(zero).valid);edge.consume(zero);CHECK(edge.channels[0].at(0).buses[0].count==1);
  auto broadcast=received(31);broadcast.validity=1;broadcast.messageType=8;broadcast.vendorStatus=0x200;
  CHECK(!aero::protocolIssues(broadcast,aero::interpret(broadcast)).noResponse);edge.consume(broadcast);
  CHECK(edge.channels[0].at(31).buses[0].count==1&&edge.channels[0].at(31).buses[0].noResponse==0);
  auto absent=received();absent.validity=0;absent.commandWord=9;
  CHECK(!aero::protocolIssues(absent,aero::interpret(absent)).illegalMode);edge.consume(absent);CHECK(edge.channels[0].at(32).buses[0].count==1);
  auto rtToRt=received();rtToRt.commandWord=(5<<11)|(2<<5)|1;rtToRt.commandWord2=(3<<11)|(1<<10)|(7<<5)|1;
  rtToRt.validity=15;rtToRt.messageType=2;rtToRt.statusWord=3<<11;rtToRt.statusWord2=5<<11;
  CHECK(aero::interpret(rtToRt).valid);const auto uniqueBefore=edge.totalCommands[0];edge.consume(rtToRt);
  CHECK(edge.totalCommands[0]==uniqueBefore+1&&edge.channels[0].at(3).buses[0].count==1&&edge.channels[0].at(5).buses[0].count==1);
  auto both=received(3);both.commandWord=(3<<11)|9;both.messageType=15;both.words.clear();both.vendorStatus=0x200;both.statusWord=7<<11;
  const auto issues=aero::protocolIssues(both,aero::interpret(both));
  CHECK(issues.noResponse&&issues.illegalMode&&issues.wrongTadr&&issues.invalid);
  const auto invalidBefore=edge.invalidCommands[0];edge.consume(both);CHECK(edge.invalidCommands[0]==invalidBefore+1);
  CHECK(edge.channels[0].at(3).buses[0].noResponse==1&&edge.channels[0].at(3).buses[0].illegalMode==1&&edge.channels[0].at(3).buses[0].wrongTadr==1);
  auto unexpectedStatus=received(0);unexpectedStatus.statusWord=1<<11;
  CHECK(aero::protocolIssues(unexpectedStatus,aero::interpret(unexpectedStatus)).wrongTadr);
  unexpectedStatus.validity=1;CHECK(!aero::protocolIssues(unexpectedStatus,aero::interpret(unexpectedStatus)).wrongTadr);
  auto simulated=received();simulated.source=0;simulated.vendorStatus=0x200;
  CHECK(!aero::protocolIssues(simulated,aero::interpret(simulated)).noResponseAvailable);
  auto invalidChannel=received();invalidChannel.channel=0;const auto revision=edge.revision;edge.consume(invalidChannel);CHECK(edge.revision==revision);

  aero::RtMonitorFrame large;auto &counter=large.channels[0][3].buses[0];counter.count=(std::uint64_t(1)<<32)+11;
  large.totalCommands[0]=counter.count;large.consume(received());CHECK(counter.count==(std::uint64_t(1)<<32)+12&&large.totalCommands[0]==counter.count);
  auto other=received(3,1,2);large.consume(other);CHECK(large.channels[1].at(3).buses[1].count==1&&large.totalCommands[1]==1);

  // Full-index visitor must not count only the 200,000-record analysis cache or
  // the 8,192-record replay payload window. Aggregation itself remains bounded.
  const auto path=std::filesystem::temp_directory_path()/("aerotrace-rt-stats-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".atd");
  constexpr unsigned fullCount=210123;
  {aero::Recorder recorder;CHECK(recorder.open(path));auto r=received();for(unsigned i=0;i<fullCount;++i){r.id=i+1;r.bus=i%2;r.timestampNs=i*2000;CHECK(recorder.append(r));}CHECK(recorder.close());}
  {
    aero::RecordingIndex index;aero::RtMonitorFrame replay;const auto status=index.open(path,[&](const auto &r){replay.consume(r);});
    CHECK(status.error.empty()&&status.count==fullCount&&index.size()==fullCount&&replay.totalCommands[0]==fullCount);
    CHECK(replay.channels[0].at(3).buses[0].count+replay.channels[0].at(3).buses[1].count==fullCount&&replay.channels[0].at(3).routes.size()==2);
  }
  {aero::RecordingIndex index;unsigned visited=0;const auto status=index.open(path,[&](const auto &){if(++visited==1000)throw std::runtime_error("Recording load cancelled");});
    CHECK(status.error=="Recording load cancelled"&&visited==1000&&index.size()==0);}
  std::filesystem::remove(path);
  std::cout<<"PASS: 632A+632B+40illegal=1304; typed independent issues; RT0/broadcast/unclassified/RT-to-RT; 64-bit counts; full-file replay and cancellation\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
