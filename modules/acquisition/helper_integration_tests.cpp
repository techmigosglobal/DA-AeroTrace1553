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
  SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",L"crash");check(source->open(config).empty(),"Crash fixture open failed");auto startError=source->startMonitoring();bool sawFailure=!startError.empty();for(unsigned i=0;i<50&&!sawFailure;++i){SourceBatch b;sawFailure=!source->poll(b,{}).empty();Sleep(20);}check(sawFailure,"Helper crash was not reported");source->close();
  SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",L"queue");check(source->open(config).empty(),"Queue fixture open failed");check(source->startMonitoring().empty(),"Queue fixture start failed");Sleep(2500);bool queueFailure=false;for(unsigned i=0;i<100&&!queueFailure;++i){SourceBatch b;queueFailure=!source->poll(b,{}).empty();Sleep(5);}check(queueFailure,"Backpressure did not produce an explicit failure");source->close();SetEnvironmentVariableW(L"AEROTRACE_HELPER_FIXTURE_SCENARIO",nullptr);
  std::cout<<"PASS helper integration: process boundary, MTI/classic raw fidelity, recording/replay, stop reconciliation, child crash, bounded backpressure; SDK fixture only\n";return 0;
}catch(const std::exception &e){std::cerr<<"Helper integration: "<<e.what()<<'\n';return 1;}}
