#include "helper_packs.hpp"
#include "helper_windows.hpp"
#include "source.hpp"
#include <iostream>
#include <thread>
using namespace aero;
using namespace aero::helper;
namespace {void check(bool value,const std::string &text){if(!value)throw std::runtime_error(text);}CaptureConfig configuration(const std::filesystem::path &pack){CaptureConfig c;c.backend=DdcBackend::HelperX86;c.connectionPolicy=DdcConnectionPolicy::Compatibility;c.helperPack=pack;c.enabled={true,true,true,true};return c;}}
int main(int argc,char **argv){try{
  if(argc!=2){std::cerr<<"Usage: helper_integration_tests fixture-helper-pack.json\n";return 2;}SetEnvironmentVariableW(L"AEROTRACE_HELPER_ALLOW_FIXTURES",L"1");SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",nullptr);const auto pack=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(argv[1])));auto descriptor=readDdcHelperPack(pack);check(descriptor.error.empty(),descriptor.error);check(descriptor.fixture,"Integration tests require a test-only helper pack");
  auto source=makeDdcHelperSource();auto config=configuration(pack);config.connectionPolicy=DdcConnectionPolicy::QualifiedAutomatic;check(!source->open(config).empty(),"Unqualified automatic helper unexpectedly connected");config.connectionPolicy=DdcConnectionPolicy::Compatibility;
  for(auto mode:{DdcReceiveMode::MTI,DdcReceiveMode::LegacyMT}){
    config.receiveMode=mode;check(source->open(config).empty(),"Fixture helper open failed: "+source->lastFailure().detail);check(source->identity().runtimeSha256==descriptor.runtimeSha256,"Resolved helper runtime identity missing");check(source->startMonitoring().empty(),"Fixture helper start failed");std::vector<Record> records;std::size_t packets=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(records.size()<10000&&std::chrono::steady_clock::now()<deadline){SourceBatch b;auto error=source->poll(b,{});check(error.empty(),error);records.insert(records.end(),b.records.begin(),b.records.end());packets+=b.packets.size();}
    check(source->stopMonitoring().empty(),"Fixture helper stop acknowledgement failed");bool more=true;while(more){SourceBatch b;auto error=source->drainNext(b,more);check(error.empty(),error);records.insert(records.end(),b.records.begin(),b.records.end());packets+=b.packets.size();}
    check(records.size()==10000&&packets==6,"Helper exact records/raw packet totals differ");for(unsigned i=0;i<records.size();++i){const auto &r=records[i];check(r.id==i+1&&r.channel==i%4/2+1&&r.bus==i%2&&r.deviceTicks==i+100&&r.tickResolutionNs==2000&&r.commandWord2==0x2842&&r.statusWord2==0x2800&&r.validity==15&&r.source==(mode==DdcReceiveMode::MTI?1:2),"Helper record provenance differs");}
    auto path=std::filesystem::temp_directory_path()/("aerotrace-helper-"+nonce()+".atd");Recorder recorder;check(recorder.open(path)&&recorder.appendBatch(records)&&recorder.close(),"Helper recording failed");auto replay=read(path);check(replay.error.empty()&&replay.records==records,"Helper recording/replay exact reconciliation failed");std::filesystem::remove(path);source->close();check(source->closeError().empty(),"Helper close failed: "+source->closeError());
  }
  SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",L"crash");check(source->open(config).empty(),"Crash fixture open failed");auto startError=source->startMonitoring();bool sawFailure=!startError.empty();for(unsigned i=0;i<50&&!sawFailure;++i){SourceBatch b;sawFailure=!source->poll(b,{}).empty();Sleep(20);}check(sawFailure,"Helper crash was not reported");source->close();check(source->closeError().empty(),"Dead helper close reported SDK cleanup failure: "+source->closeError());
  SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",nullptr);check(source->open(config).empty(),"Fresh connection after transport failure was rejected");check(source->startMonitoring().empty(),"Fresh connection start failed after transport failure");source->close();check(source->closeError().empty(),"Fresh connection close failed: "+source->closeError());
  SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",L"queue");check(source->open(config).empty(),"Queue fixture open failed");check(source->startMonitoring().empty(),"Queue fixture start failed");Sleep(2500);bool queueFailure=false;for(unsigned i=0;i<100&&!queueFailure;++i){SourceBatch b;queueFailure=!source->poll(b,{}).empty();Sleep(5);}check(queueFailure,"Backpressure did not produce an explicit failure");source->close();SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",nullptr);
  const auto openScenario=[&](const wchar_t *name){
    SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",name);
    check(source->open(config).empty(),"Scenario open failed");
    check(source->startMonitoring().empty(),"Scenario start failed");
  };
  openScenario(L"quiet-cycle");
  std::uint64_t quietRecords=0,quietPackets=0;
  const auto quietDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(32);
  while(quietRecords<10000 && std::chrono::steady_clock::now()<quietDeadline) {
    SourceBatch batch;
    const auto error=source->poll(batch,{});
    check(error.empty(),error);
    for(const auto &record:batch.records)check(record.id==++quietRecords,"Quiet-cycle record sequence mismatch");
    quietPackets+=batch.packets.size();
  }
  SourceBatch quietTail;
  check(source->finish(quietTail).empty(),"Quiet-cycle final acknowledgement failed");
  check(quietRecords==10000 && quietPackets==6 && quietTail.records.empty(),"Quiet-cycle data reconciliation failed");
  source->close();check(source->closeError().empty(),"Quiet-cycle close failed");
  for(const auto &[scenario,expected]:{std::pair{L"hang","SDK operation stalled"},std::pair{L"no-heartbeat","heartbeat timed out"}}) {
    openScenario(scenario);
    std::string error;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(16);
    while(error.empty() && std::chrono::steady_clock::now()<deadline){SourceBatch batch;error=source->poll(batch,{});}
    check(error.find(expected)!=std::string::npos,"Wrong helper watchdog result: "+error);
    source->close();check(source->closeError().empty(),"Confirmed helper exit blocked reconnect");
  }
  openScenario(L"missing-end");
  SourceBatch first;
  const auto receiveDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(first.records.empty() && std::chrono::steady_clock::now()<receiveDeadline) {
    const auto error=source->poll(first,{});
    check(error.empty(),error);
  }
  check(!first.records.empty() && first.records.front().id==1 && !first.packets.empty(),"Missing-End fixture produced no raw batch");
  SourceBatch incomplete;
  const auto missingEnd=source->finish(incomplete);
  check(missingEnd.find("acknowledgement deadline exceeded")!=std::string::npos,"Missing final data acknowledgement accepted");
  for(std::size_t i=0;i<incomplete.records.size();++i)
    check(incomplete.records[i].id==first.records.size()+i+1,"Accepted records lost after missing acknowledgement");
  source->close();check(source->closeError().empty(),"Missing acknowledgement blocked fresh helper after exit");
  openScenario(L"cleanup-failure");
  SourceBatch clean;
  check(source->finish(clean).empty(),"Cleanup fixture stop failed");
  source->close();
  check(source->closeError()=="Fixture SDK cleanup failed","Explicit SDK cleanup failure was lost");
  check(!source->open(config).empty(),"SDK cleanup failure failed to block reopen");
  SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",nullptr);
  std::cout<<"PASS helper integration: process boundary, MTI/classic raw fidelity, recording/replay, stop reconciliation, child crash, bounded backpressure, quiet-cycle, watchdogs, missing final acknowledgement, explicit cleanup failure; SDK fixture only\n";return 0;
}catch(const std::exception &e){std::cerr<<"Helper integration: "<<e.what()<<'\n';return 1;}}
