// Passive bench evidence collection. Never transmits or configures a generator.
#include "acquisition.hpp"
#include "ddc_runtime.hpp"
#include "helper_packs.hpp"
#include "protocol.hpp"
#include "capture_time.hpp"
#include "evidence_identity.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
using namespace std::chrono;
struct Stream {
  std::uint64_t count{}, valid{}, responded{}, invalid{}, backwards{}, first{}, last{};
};
unsigned number(const std::string &text,unsigned maximum) {
  std::size_t used{};const auto value=std::stoul(text,&used);
  if(used!=text.size()||value>maximum)throw std::runtime_error("Numeric argument outside allowed range: "+text);
  return unsigned(value);
}
void runtimeJson(std::ostream &out,const aero::DdcRuntimeSelection &selection) {
  out<<"{\"selected\":"<<aero::jsonString(selection.path.string())<<",\"error\":"<<aero::jsonString(selection.error)<<",\"candidates\":[";
  bool comma=false;for(const auto &c:selection.candidates){if(comma)out<<',';comma=true;
    out<<"{\"path\":"<<aero::jsonString(c.path.string())<<",\"origin\":"<<aero::jsonString(c.origin)
      <<",\"file_version\":"<<aero::jsonString(c.version)<<",\"api_raw\":"<<c.api
      <<",\"mt\":"<<(c.legacyMt?"true":"false")<<",\"mti\":"<<(c.mti?"true":"false")
      <<",\"driver_version_match\":"<<(c.driverMatch?"true":"false")<<",\"error\":"<<aero::jsonString(c.error)<<'}';}
  out<<"]}";
}
}
int main(int argc,char **argv) {
  std::filesystem::path output;bool ownsOutput=false;
  try {
    aero::CaptureConfig config;config.enabled={true,true,true,true};config.runId="qualification-"+std::to_string(system_clock::now().time_since_epoch().count());
    unsigned seconds=60,cycles=1;std::string sourceLabel="physical-bench";
    for(int i=1;i<argc;++i){const std::string key=argv[i];
      if(key=="--help"){std::cout<<"aerotrace_qualification --output <new-directory> [--seconds 60] [--cycles 1] [--streams 1A,1B,2A,2B] [--logical1 0] [--logical2 1] [--runtime <emacepl.dll>] [--backend native-x64|helper-x86] [--receive-mode auto|mti|legacy-mt] [--connection-policy qualified|compatibility] [--helper-pack <helper-pack.json>] [--source-label physical-bench|sdk-fixture]\n";return 0;}
      if(i+1>=argc)throw std::runtime_error("Missing value for "+key);const std::string value=argv[++i];
      if(key=="--output")output=std::filesystem::path(std::u8string(value.begin(),value.end()));
      else if(key=="--seconds"){seconds=number(value,7*24*3600);if(!seconds)throw std::runtime_error("Duration must be positive");}
      else if(key=="--cycles"){cycles=number(value,1000);if(!cycles)throw std::runtime_error("Cycles must be positive");}
      else if(key=="--runtime")config.runtime=std::filesystem::path(std::u8string(value.begin(),value.end()));
      else if(key=="--logical1")config.logicalDevices[0]=number(value,31);
      else if(key=="--logical2")config.logicalDevices[1]=number(value,31);
      else if(key=="--backend") {if(value=="native-x64")config.backend=aero::DdcBackend::NativeX64;else if(value=="helper-x86")config.backend=aero::DdcBackend::HelperX86;else throw std::runtime_error("Unknown DDC backend");}
      else if(key=="--receive-mode") {if(value=="auto")config.receiveMode=aero::DdcReceiveMode::Auto;else if(value=="mti")config.receiveMode=aero::DdcReceiveMode::MTI;else if(value=="legacy-mt")config.receiveMode=aero::DdcReceiveMode::LegacyMT;else throw std::runtime_error("Unknown receive mode");}
      else if(key=="--connection-policy") {if(value=="qualified")config.connectionPolicy=aero::DdcConnectionPolicy::QualifiedAutomatic;else if(value=="compatibility")config.connectionPolicy=aero::DdcConnectionPolicy::Compatibility;else throw std::runtime_error("Unknown connection policy");}
      else if(key=="--helper-pack")config.helperPack=std::filesystem::path(std::u8string(value.begin(),value.end()));
      else if(key=="--source-label"){if(value!="physical-bench"&&value!="sdk-fixture")throw std::runtime_error("Unknown evidence source");sourceLabel=value;}
      else if(key=="--streams") {config.enabled={};std::istringstream list(value);std::string item;bool any=false;
        if(value.empty()||value.back()==',')throw std::runtime_error("Invalid stream list");
        while(std::getline(list,item,',')){
          unsigned k=item=="1A"?0:item=="1B"?1:item=="2A"?2:item=="2B"?3:4;
          if(k==4)throw std::runtime_error("Unknown stream: "+item);config.enabled[k]=true;any=true;}
        if(!any)throw std::runtime_error("Select at least one stream");
      } else throw std::runtime_error("Unknown argument: "+key);
    }
    if(output.empty())throw std::runtime_error("Specify --output with a new evidence directory; use --help");
    if(!std::filesystem::create_directory(output))throw std::runtime_error("Output directory must be new; previous evidence is never overwritten");
    ownsOutput=true;
    std::ofstream runtime(output/"runtime.json");
    aero::DdcRuntimeSelection selection;
    if(config.backend==aero::DdcBackend::HelperX86){
      const auto pack=aero::readDdcHelperPack(config.helperPack);
      selection.path=pack.runtimePath;selection.error=pack.error;
      runtime<<"{\"backend\":\"helper_x86\",\"manifest\":"<<aero::jsonString(config.helperPack.string())<<",\"runtime\":"<<aero::jsonString(pack.runtimePath.string())<<",\"helper_sha256\":"<<aero::jsonString(pack.helperSha256)<<",\"runtime_sha256\":"<<aero::jsonString(pack.runtimeSha256)<<",\"error\":"<<aero::jsonString(pack.error)<<'}';
    }else {selection=aero::selectDdcRuntime(config.runtime);runtimeJson(runtime,selection);}
    runtime.flush();
    if(!runtime)throw std::runtime_error("Cannot save runtime evidence");
    const auto executableHash=bench::executableSha256();
    std::ofstream report(output/"runs.jsonl");if(!report)throw std::runtime_error("Cannot create run report");
    if(!selection.error.empty())throw std::runtime_error(selection.error);config.runtime=selection.path;
    std::ofstream connection(output/"connection.jsonl");
    if(!connection)throw std::runtime_error("Cannot create connection diagnostics");
    config.diagnostic=[&](const auto &operation,int result,const auto &detail){connection<<"{\"operation\":"<<aero::jsonString(operation)<<",\"result\":"<<result<<",\"detail\":"<<aero::jsonString(detail)<<"}\n";connection.flush();};
    aero::Acquisition acquisition;
    if(!acquisition.connectConfigured(config))throw std::runtime_error(acquisition.metrics(false).error);
    bool complete=true;
    for(unsigned cycle=1;cycle<=cycles;++cycle){
      const auto file=output/("cycle-"+std::to_string(cycle)+".atd");
      if(!acquisition.startConfigured(file,config))throw std::runtime_error(acquisition.metrics(false).error);
      const auto began=steady_clock::now();auto next=began;const auto end=began+std::chrono::seconds(seconds);
      while(acquisition.active()&&steady_clock::now()<end){
        acquisition.drain();const auto now=steady_clock::now();
        if(now>=next){const auto m=acquisition.metrics(false);std::cout<<"cycle="<<cycle<<" recorded="<<m.recorded<<" loss="<<m.hardwareLost<<" overflow="<<m.hardwareOverflows<<std::endl;next=now+std::chrono::seconds(1);}
        std::this_thread::sleep_for(milliseconds(10));
      }
      acquisition.stopRecording();acquisition.drain();const auto m=acquisition.metrics(false);std::array<Stream,4> streams{};
      const auto scan=aero::scanRecording(file,[&](const aero::Record &r,auto){
        if(r.channel<1||r.channel>2||r.bus>1)return true;auto &s=streams[(r.channel-1)*2+r.bus];const auto p=aero::interpret(r);
        if(!s.count)s.first=r.timestampNs;else if(r.timestampNs<s.last)++s.backwards;s.last=r.timestampNs;++s.count;
        if(p.valid)++s.valid;else ++s.invalid;
        if(p.valid&&(p.statuses[0].present||p.statuses[1].present))++s.responded;return true;
      });
      bool traffic=true,responses=true;for(unsigned k=0;k<4;++k)if(config.enabled[k]){traffic&=streams[k].count>0;responses&=streams[k].responded>0;}
      const bool intact=!m.failed&&!m.hardwareLost&&!m.hardwareOverflows&&!scan.recovered&&scan.error.empty()&&scan.count==m.recorded;
      const auto verdict=!intact?"capture_failed":!traffic?"missing_required_stream":!responses?"response_coverage_missing":"capture_evidence_complete";
      complete&=intact&&traffic&&responses;
      report<<"{\"schema\":\"aerotrace.qualification.v1\",\"run_id\":"<<aero::jsonString(config.runId)<<",\"source\":"<<aero::jsonString(sourceLabel)
        <<",\"executable_sha256\":"<<aero::jsonString(executableHash)<<",\"evidence_directory\":"<<aero::jsonString(bench::pathText(std::filesystem::absolute(output)))
        <<",\"cycle\":"<<cycle<<",\"requested_seconds\":"<<seconds<<",\"elapsed_ms\":"<<duration_cast<milliseconds>(steady_clock::now()-began).count()
        <<",\"recording\":"<<aero::jsonString(file.string())<<",\"recorded\":"<<m.recorded<<",\"hardware_lost\":"<<m.hardwareLost
        <<",\"hardware_overflows\":"<<m.hardwareOverflows<<",\"display_skips\":"<<m.displayDrops<<",\"queue_high_water\":"<<m.queueHighWater<<",\"max_worker_batch_ms\":"<<m.maxBatchMilliseconds<<",\"connected_after_stop\":"<<(acquisition.connected()?"true":"false")
        <<",\"status\":"<<aero::jsonString(intact&&traffic&&responses?"PASS":"FAIL")<<",\"physical_qualification\":\"NOT RUN: independent reference comparison and operator sign-off required\",\"verdict\":"<<aero::jsonString(verdict)<<",\"qualification\":\"Reference counts, payloads, timing accuracy and physical reconnection require independent bench acceptance\",\"error\":"<<aero::jsonString(m.error+scan.error)<<",\"streams\":[";
      for(unsigned k=0;k<4;++k){if(k)report<<',';const auto &s=streams[k];report<<"{\"channel\":"<<k/2+1<<",\"bus\":"<<aero::jsonString(k%2?"B":"A")<<",\"required\":"<<(config.enabled[k]?"true":"false")
        <<",\"records\":"<<s.count<<",\"valid\":"<<s.valid<<",\"valid_with_response\":"<<s.responded<<",\"invalid\":"<<s.invalid<<",\"clock_backwards\":"<<s.backwards<<",\"first_ns\":"<<s.first<<",\"last_ns\":"<<s.last<<'}';}
      report<<"],\"stage_timings\":{";
      for(unsigned k=0;k<aero::TimingStageCount;++k){if(k)report<<',';const auto &h=m.timings[k];report<<aero::jsonString(aero::timingNames[k])<<":{\"samples\":"<<h.count<<",\"p95_upper_ns\":"<<h.quantile(95)<<",\"p99_upper_ns\":"<<h.quantile(99)<<",\"max_ns\":"<<h.maximumNs<<'}';}
      report<<"}}\n";report.flush();if(!report)throw std::runtime_error("Qualification report write failed");
      if(m.failed||!acquisition.connected())break;
    }
    acquisition.stop();if(acquisition.error())complete=false;
    std::cout<<"Evidence: "<<output<<"; hardware qualification requires reference comparison and operator sign-off\n";
    return complete?0:4;
  }catch(const std::exception &e){
    if(ownsOutput){std::ofstream failed(output/"failure.json");failed<<"{\"schema\":\"aerotrace.qualification.failure.v1\",\"status\":\"FAIL\",\"physical_qualification\":\"NOT RUN\",\"executable_sha256\":"<<aero::jsonString(bench::executableSha256())<<",\"evidence_directory\":"<<aero::jsonString(bench::pathText(std::filesystem::absolute(output)))<<",\"error\":"<<aero::jsonString(e.what())<<"}\n";}
    std::cerr<<"Qualification: "<<e.what()<<'\n';return 2;
  }
}
