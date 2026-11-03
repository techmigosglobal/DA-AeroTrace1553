#include "acquisition.hpp"
#include "ddc_runtime.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x)) throw std::runtime_error(#x);}while(false)
int main(int argc,char **argv) {
  using namespace std::chrono;
  const auto root=std::filesystem::temp_directory_path()/("aerotrace-ddc-"+std::to_string(steady_clock::now().time_since_epoch().count()));
  try {
    CHECK(argc==4);std::filesystem::create_directory(root);
    aero::CaptureConfig config;config.runtime=std::filesystem::absolute(argv[1]);config.enabled={true,true,true,true};
    CHECK(aero::inspectDdcRuntime(config.runtime).available);
#ifdef _WIN32
    const auto explicitRuntime=aero::selectDdcRuntime(config.runtime);
    CHECK(explicitRuntime.error.empty() && explicitRuntime.path==config.runtime);
    CHECK(explicitRuntime.candidates.size()==1 && explicitRuntime.candidates[0].origin=="manual override");
    CHECK(explicitRuntime.candidates[0].usable);
    CHECK(explicitRuntime.candidates[0].legacyMt && explicitRuntime.candidates[0].mti);
    {
      wchar_t saved[32768]{};CHECK(GetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",saved,32768)>0);
      const std::filesystem::path good(saved);
      SetEnvironmentVariableA("AEROTRACE_DDC_PROVIDER",AEROTRACE_TEST_BAD_PROVIDER);
      CHECK(aero::inspectDdcRuntime(config.runtime).message.find("ABI mismatch")!=std::string::npos);
      SetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",config.runtime.c_str());
      CHECK(aero::inspectDdcRuntime(config.runtime).message.find("lacks aerotrace_provider_get_api")!=std::string::npos);
      const auto wrong=root/"provider-x86.dll";std::filesystem::copy_file(good,wrong);
      {std::fstream image(wrong,std::ios::binary|std::ios::in|std::ios::out);std::uint32_t offset{};image.seekg(0x3c);image.read(reinterpret_cast<char *>(&offset),4);image.seekp(offset+4);const std::uint16_t machine=0x14c;image.write(reinterpret_cast<const char *>(&machine),2);}
      SetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",wrong.c_str());CHECK(!aero::inspectDdcRuntime(config.runtime).available);
      const auto ambiguous=root/"ambiguous-providers";std::filesystem::create_directory(ambiguous);
      std::filesystem::copy_file(good,ambiguous/"first.dll");std::filesystem::copy_file(good,ambiguous/"second.dll");
      {std::ofstream changed(ambiguous/"second.dll",std::ios::binary|std::ios::app);changed<<'x';}
      SetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",ambiguous.c_str());CHECK(aero::inspectDdcRuntime(config.runtime).message.find("Ambiguous")!=std::string::npos);
      SetEnvironmentVariableW(L"AEROTRACE_DDC_PROVIDER",good.c_str());CHECK(aero::inspectDdcRuntime(config.runtime).available);
      auto one=aero::makeDdcSource(),two=aero::makeDdcSource();CHECK(one->open(config).empty());CHECK(two->open(config).find("already owns")!=std::string::npos);one->close();CHECK(two->open(config).empty());two->close();
    }
    {
      struct RestoreSdkRoot {
        std::wstring value;bool present{};
        RestoreSdkRoot(){wchar_t saved[32768]{};const auto length=GetEnvironmentVariableW(L"DDC_SDK_ROOT",saved,32768);CHECK(length<32768);present=length>0;value=saved;}
        ~RestoreSdkRoot(){SetEnvironmentVariableW(L"DDC_SDK_ROOT",present?value.c_str():nullptr);}
      } restore;
      // Folder names are deliberately unrelated to the SDK used to compile this test.
      for(const auto layout:{"Lib/x64/Release/emacepl.dll","libraries/emacepl/bin/x64/Release/emacepl.dll","emacepl.dll"}) {
        const auto sdkRoot=root/("PC configured SDK "+std::to_string(std::string(layout).size()));
        const auto dll=sdkRoot/layout;std::filesystem::create_directories(dll.parent_path());std::filesystem::copy_file(config.runtime,dll);
        const auto folder=aero::selectDdcRuntime(sdkRoot);
        CHECK(folder.error.empty() && folder.path==dll && folder.candidates[0].origin=="manual SDK folder");
        CHECK(SetEnvironmentVariableW(L"DDC_SDK_ROOT",sdkRoot.c_str()));
        const auto local=aero::selectDdcRuntime();
        CHECK(local.error.empty() && local.path==dll && local.candidates[0].origin=="DDC_SDK_ROOT");
        // A per-application exact path still overrides the machine-level default.
        CHECK(aero::selectDdcRuntime(config.runtime).path==config.runtime);
      }
      const auto empty=root/"SDK missing runtime";std::filesystem::create_directory(empty);
      CHECK(SetEnvironmentVariableW(L"DDC_SDK_ROOT",empty.c_str()));
      const auto missing=aero::selectDdcRuntime();
      CHECK(missing.path.empty() && missing.error.find("Configured DDC SDK folder")!=std::string::npos);
      const auto broken=empty/"emacepl.dll";{std::ofstream out(broken);out<<"not a PE image";}
      const auto invalidSdk=aero::selectDdcRuntime();
      CHECK(invalidSdk.path.empty() && invalidSdk.error.find("x64 PE")!=std::string::npos);
      const auto conflict=root/"SDK conflicting interfaces";
      std::filesystem::create_directories(conflict/"Lib/x64/Release");
      std::filesystem::create_directories(conflict/"libraries/emacepl/bin/x64/Release");
      std::filesystem::copy_file(config.runtime,conflict/"Lib/x64/Release/emacepl.dll");
      std::filesystem::copy_file(std::filesystem::absolute(argv[2]),conflict/"libraries/emacepl/bin/x64/Release/emacepl.dll");
      CHECK(aero::selectDdcRuntime(conflict).error.find("conflicting receive runtimes")!=std::string::npos);
    }
    for(const auto api:{"0x36f","0x4a2","0x7ff"}) {
      SetEnvironmentVariableA("AEROTRACE_TEST_DDC_API",api);
      const auto versioned=aero::selectDdcRuntime(config.runtime);
      CHECK(versioned.error.empty() && versioned.candidates[0].api==std::strtoul(api,nullptr,0));
    }
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_API",nullptr);
    const auto partial=aero::selectDdcRuntime(std::filesystem::absolute(argv[3]));
    CHECK(partial.error.empty() && partial.candidates[0].legacyMt && !partial.candidates[0].mti);
    CHECK(partial.candidates[0].missingMti==std::vector<std::string>{"aceMtiGetMetrics"});
    CHECK(aero::selectDdcRuntime(root/"absent.dll").error.find("does not exist")!=std::string::npos);
    const auto invalid=root/"invalid.dll";{std::ofstream out(invalid,std::ios::binary);out<<"not a PE image";}
    CHECK(aero::selectDdcRuntime(invalid).error.find("x64 PE")!=std::string::npos);
    const auto wrongMachine=root/"x86.dll";std::filesystem::copy_file(config.runtime,wrongMachine);
    {std::fstream image(wrongMachine,std::ios::binary|std::ios::in|std::ios::out);std::uint32_t offset{};image.seekg(0x3c);image.read(reinterpret_cast<char *>(&offset),4);image.seekp(offset+4);const std::uint16_t x86=0x14c;image.write(reinterpret_cast<const char *>(&x86),2);}
    CHECK(aero::selectDdcRuntime(wrongMachine).error.find("x64 PE")!=std::string::npos);
    wchar_t system[32768]{};CHECK(GetSystemDirectoryW(system,32768));
    const auto nonDdc=aero::selectDdcRuntime(std::filesystem::path(system)/"kernel32.dll");
    CHECK(nonDdc.error.find("Missing DDC export")!=std::string::npos);
#endif

#ifdef _WIN32
    const auto fake=LoadLibraryW(config.runtime.c_str());CHECK(fake);
    const auto count=reinterpret_cast<int(WINAPI *)()>(GetProcAddress(fake,"aeroTestStartCount"));
    const auto reset=reinterpret_cast<void(WINAPI *)()>(GetProcAddress(fake,"aeroTestResetStarts"));CHECK(count && reset);
    const auto openCount=reinterpret_cast<int(WINAPI *)()>(GetProcAddress(fake,"aeroTestOpenCount"));CHECK(openCount);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_REQUIRE_DUAL","1");
#endif
    aero::Acquisition acquisition;
    CHECK(acquisition.setProfile({aero::Signal{"TEMP","degC"}}).empty());
    CHECK(acquisition.startConfigured(root/"capture.atd",config));
    auto deadline=steady_clock::now()+seconds(3);
    while(acquisition.metrics().recorded<4 && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(2));
    acquisition.stop();const auto metrics=acquisition.metrics();
    CHECK(!metrics.failed && metrics.recorded==4 && !metrics.hardwareLost);
    auto raw=aero::read(root/"capture.atd");CHECK(raw.error.empty() && !raw.recovered && raw.records.size()==4);
    CHECK(raw.records[0].source==1 && raw.records[0].deviceTicks==100000 && raw.records[0].timestampNs==200000000 && raw.records[0].tickResolutionNs==2000);
    CHECK(raw.records[0].vendorStatus==0x8000 && raw.records[0].validity==5);
    CHECK(raw.records[2].channel==2 && raw.records[2].bus==1 && raw.records[0].hostMonotonicNs>0);
    CHECK(std::filesystem::file_size(root/"capture.atd.ch1.ch10")==72);
    CHECK(std::filesystem::file_size(root/"capture.atd.ch2.ch10")==72);
    CHECK(acquisition.liveFrame()->values[0].value.value()==12.01);
#ifdef _WIN32
    // All nonempty stream masks: A/B independently and concurrently on both channels.
    // This exercises the adapter contract, not USB/driver or physical bus behavior.
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_REQUIRE_DUAL",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_BOTH_BUSES","1");
    for(unsigned mask=1;mask<16;++mask) {
      unsigned expected=0;for(unsigned k=0;k<4;++k){config.enabled[k]=(mask&(1u<<k))!=0;expected+=config.enabled[k];}
      const auto path=root/("streams-"+std::to_string(mask)+".atd");
      CHECK(acquisition.startConfigured(path,config));deadline=steady_clock::now()+seconds(2);
      while(acquisition.metrics(false).recorded<expected && steady_clock::now()<deadline)std::this_thread::sleep_for(milliseconds(1));
      acquisition.stop();CHECK(!acquisition.error());const auto all=aero::read(path);CHECK(all.records.size()==expected);
      std::array<unsigned,4> counts{};for(const auto &r:all.records)++counts[(r.channel-1)*2+r.bus];
      for(unsigned k=0;k<4;++k)CHECK(counts[k]==unsigned(config.enabled[k]));
    }
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_BOTH_BUSES",nullptr);config.enabled={true,true,true,true};
    auto savedRuntime=config.runtime;config.runtime=std::filesystem::absolute(argv[3]);
    CHECK(!acquisition.connectConfigured(config));CHECK(acquisition.metrics(false).error.find("aceMtiGetMetrics")!=std::string::npos);
    config.runtime=savedRuntime;
#endif
#ifdef _WIN32
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_LOSS","1");
#endif
    CHECK(acquisition.startConfigured(root/"loss.atd",config));
    deadline=steady_clock::now()+seconds(3);
    while(acquisition.active() && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(10));
    acquisition.stop();CHECK(acquisition.error() && acquisition.metrics().hardwareLost>0);
#ifdef _WIN32
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_LOSS",nullptr);
    reset();SetEnvironmentVariableA("AEROTRACE_TEST_DDC_FAIL_SECOND","1");
    CHECK(!acquisition.startConfigured(root/"second-failed.atd",config));
    CHECK(count()==0 && acquisition.metrics().devices[1].phase==aero::DevicePhase::Failed);
    CHECK(!std::filesystem::exists(root/"second-failed.atd"));
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_FAIL_SECOND",nullptr);
    for(const auto env:{"AEROTRACE_TEST_DDC_UNUSED_TIME","AEROTRACE_TEST_DDC_RTC_WRAP"}) {
      SetEnvironmentVariableA(env,"1");const auto file=root/(std::string(env)+".atd");
      CHECK(acquisition.startConfigured(file,config));deadline=steady_clock::now()+seconds(3);
      while(acquisition.metrics().recorded<4 && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(2));
      acquisition.stop();auto timed=aero::read(file);CHECK(!acquisition.error() && timed.records.size()==4);
      if(std::string(env).find("UNUSED")!=std::string::npos) CHECK(timed.records[0].deviceTicks==100000 && timed.records[0].timestampNs==200000000);
      else CHECK(timed.records[1].deviceTicks==0 && timed.records[1].timestampNs==((1ull<<48)*2000) && timed.records[1].timestampNs-timed.records[0].timestampNs==2000);
      SetEnvironmentVariableA(env,nullptr);
    }
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_ABSOLUTE","1");CHECK(acquisition.startConfigured(root/"absolute.atd",config));
    deadline=steady_clock::now()+seconds(3);while(acquisition.active() && steady_clock::now()<deadline) std::this_thread::sleep_for(milliseconds(2));
    acquisition.stop();CHECK(acquisition.error() && acquisition.metrics().error.find("Unsupported DDC")!=std::string::npos);
    CHECK(std::filesystem::file_size(root/"absolute.atd.ch1.ch10")==72);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_ABSOLUTE",nullptr);
    std::ifstream diagnostics(root/"capture.atd.events.jsonl");const std::string text{std::istreambuf_iterator<char>(diagnostics),{}};
    CHECK(text.find("aceInitialize")!=std::string::npos && text.find("capture_closed")!=std::string::npos && text.find("utc_epoch_ms")!=std::string::npos);
    diagnostics.close();
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_REQUIRE_DUAL",nullptr);
#endif
#ifdef _WIN32
    for(const auto setting:{"AEROTRACE_TEST_DDC_DRIVER_OPEN"}) {
      SetEnvironmentVariableA(setting,"1");CHECK(!acquisition.startConfigured(root/"rejected.atd",config));
      CHECK(!std::filesystem::exists(root/"rejected.atd"));
      if(std::string(setting).find("DRIVER_OPEN")!=std::string::npos)CHECK(acquisition.metrics().error.find("Card Manager")!=std::string::npos);
      else CHECK(acquisition.metrics().error.find("does not support MTI")!=std::string::npos);
      SetEnvironmentVariableA(setting,nullptr);
    }
    for(const auto setting:{"AEROTRACE_TEST_DDC_DISCONNECT","AEROTRACE_TEST_DDC_OVERFLOW"}) {
      SetEnvironmentVariableA(setting,"1");CHECK(acquisition.startConfigured(root/"fault.atd",config));
      deadline=steady_clock::now()+seconds(3);while(acquisition.active()&&steady_clock::now()<deadline)std::this_thread::sleep_for(milliseconds(2));
      acquisition.stop();CHECK(acquisition.error());SetEnvironmentVariableA(setting,nullptr);
      CHECK(acquisition.connectConfigured(config));CHECK(acquisition.connected()&&!acquisition.active());
      CHECK(acquisition.startConfigured(root/"reconnected.atd",config));std::this_thread::sleep_for(milliseconds(5));acquisition.stop();CHECK(!acquisition.error());
    }
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_TAIL_ONLY","1");CHECK(acquisition.startConfigured(root/"tail.atd",config));
    std::this_thread::sleep_for(milliseconds(10));acquisition.stop();CHECK(!acquisition.error()&&acquisition.metrics().recorded==4);
    CHECK(aero::read(root/"tail.atd").records.size()==4);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_TAIL_ONLY",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_NO_MTI","1");SetEnvironmentVariableA("AEROTRACE_TEST_DDC_UNSUPPORTED","1");
    CHECK(!acquisition.startConfigured(root/"unsupported.atd",config));CHECK(acquisition.metrics().error.find("aceMTConfigure")!=std::string::npos);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_UNSUPPORTED",nullptr);
    CHECK(acquisition.startConfigured(root/"legacy.atd",config));deadline=steady_clock::now()+seconds(3);
    while(acquisition.metrics().recorded<2&&steady_clock::now()<deadline)std::this_thread::sleep_for(milliseconds(2));
    acquisition.stop();CHECK(!acquisition.error());auto legacy=aero::read(root/"legacy.atd");CHECK(legacy.records.size()==2&&legacy.records[0].source==2&&legacy.records[0].tickResolutionNs==2000&&legacy.records[0].deviceTicks==1234);
    CHECK(std::filesystem::file_size(root/"legacy.atd.ch1.mt40")==80);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_NO_MTI",nullptr);
    config.enabled={true,true,false,false};CHECK(acquisition.startConfigured(root/"single.atd",config));acquisition.stop();CHECK(!acquisition.error());
    config.enabled={true,true,true,true};
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_SLOW_OPEN","1");auto cancelled=acquisition.beginConfigured(root/"cancelled.atd",config);std::this_thread::sleep_for(milliseconds(20));acquisition.requestStop();CHECK(!cancelled.get());acquisition.stop();CHECK(openCount()==0&&!std::filesystem::exists(root/"cancelled.atd"));SetEnvironmentVariableA("AEROTRACE_TEST_DDC_SLOW_OPEN",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_START_SECOND","1");CHECK(!acquisition.startConfigured(root/"start-rollback.atd",config));CHECK(openCount()==0);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_START_SECOND",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_MIXED","1");CHECK(acquisition.startConfigured(root/"mixed.atd",config));std::this_thread::sleep_for(milliseconds(20));acquisition.stop();CHECK(!acquisition.error()&&acquisition.metrics().recorded==3);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_MIXED",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_2US_GAP","1");
    CHECK(acquisition.startConfigured(root/"native-clock.atd",config));std::this_thread::sleep_for(milliseconds(20));acquisition.stop();
    const auto nativeClock=aero::read(root/"native-clock.atd");CHECK(!acquisition.error()&&nativeClock.records.size()==4);
    CHECK(nativeClock.records[1].deviceTicks-nativeClock.records[0].deviceTicks==50000);
    CHECK(nativeClock.records[1].timestampNs-nativeClock.records[0].timestampNs==100000000);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_2US_GAP",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_FREE_UNSUPPORTED","1");
    CHECK(acquisition.startConfigured(root/"windows-caller-buffer.atd",config));std::this_thread::sleep_for(milliseconds(20));acquisition.stop();
    CHECK(!acquisition.error() && acquisition.metrics().recorded==4);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_SDK_OWNED","1");
    CHECK(acquisition.startConfigured(root/"unsupported-owned-release.atd",config));std::this_thread::sleep_for(milliseconds(20));acquisition.stop();
    CHECK(acquisition.error());
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_SDK_OWNED",nullptr);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_FREE_UNSUPPORTED",nullptr);
    for(const auto setting:{"AEROTRACE_TEST_DDC_STOP_LOSS","AEROTRACE_TEST_DDC_COUNTER_OVERFLOW","AEROTRACE_TEST_DDC_STOP_FAIL","AEROTRACE_TEST_DDC_FREE_FAIL","AEROTRACE_TEST_DDC_PACKET_FREE_FAIL"}) {
      SetEnvironmentVariableA(setting,"1");CHECK(acquisition.startConfigured(root/(std::string(setting)+".atd"),config));std::this_thread::sleep_for(milliseconds(20));acquisition.stop();CHECK(acquisition.error()&&openCount()==0);
      if(std::string(setting)=="AEROTRACE_TEST_DDC_STOP_LOSS"){CHECK(acquisition.metrics().channelLost[0]==1&&acquisition.metrics().channelLost[1]==2&&acquisition.metrics().hardwareLost==3);}
      if(std::string(setting)=="AEROTRACE_TEST_DDC_COUNTER_OVERFLOW"){CHECK(acquisition.metrics().channelOverflows[0]==1&&acquisition.metrics().channelOverflows[1]==2);}
      SetEnvironmentVariableA(setting,nullptr);
    }
    FreeLibrary(fake);
    config.runtime=std::filesystem::absolute(argv[2]);CHECK(aero::inspectDdcRuntime(config.runtime).available);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_TAIL_ONLY","1");CHECK(acquisition.startConfigured(root/"legacy-only.atd",config));acquisition.stop();CHECK(!acquisition.error()&&acquisition.metrics().recorded==2);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_TAIL_ONLY",nullptr);
    SetEnvironmentVariableA("AEROTRACE_TEST_DDC_FORCE_MTI","1");CHECK(!acquisition.startConfigured(root/"missing-mti-export.atd",config));CHECK(acquisition.metrics().error.find("aceMTIConfigure")!=std::string::npos);SetEnvironmentVariableA("AEROTRACE_TEST_DDC_FORCE_MTI",nullptr);
#endif
    config.runtime=root/"not-installed.dll";CHECK(!acquisition.startConfigured(root/"missing.atd",config));
    CHECK(acquisition.metrics().error.find("Selected DDC runtime does not exist")!=std::string::npos);
    std::filesystem::remove_all(root);
    std::cout<<"PASS: receive-only SDK calls, packet batches, raw provenance, loss fail-stop, missing runtime\n";
  } catch(const std::exception &error) {std::cerr<<error.what()<<"\nFixtures: "<<root<<'\n';return 1;}
}
