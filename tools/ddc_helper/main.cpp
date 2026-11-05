#include <map>
#include "helper_packs.hpp"
#include "helper_windows.hpp"
#include <condition_variable>
#include <deque>
#include <iostream>
#include <thread>

namespace aero {std::unique_ptr<CaptureSource> makeVendorDdcSource();}
using namespace aero;
using namespace aero::helper;
namespace {
class FixtureSource final:public CaptureSource {
  std::thread::id owner;CaptureConfig config;bool active{};unsigned position{};
  void check(){if(owner!=std::this_thread::get_id())throw std::runtime_error("Fixture SDK lifecycle changed threads");}
public:
  std::string open(const CaptureConfig &value)override{owner=std::this_thread::get_id();config=value;for(unsigned c=0;c<2;++c)if(config.enabled[c*2]||config.enabled[c*2+1]){if(config.logicalDevices[c]==31)return "Fixture assignment unavailable";if(config.deviceState)config.deviceState(c,DevicePhase::Ready,"SDK fixture only; no physical card");}return {};}
  std::string startMonitoring()override{check();active=true;position=0;return {};}
  std::string poll(SourceBatch &batch,std::stop_token)override{check();if(!active){Sleep(1);return {};}
    char scenario[64]{};GetEnvironmentVariableA("AEROTRACE_HELPER_FIXTURE_SCENARIO",scenario,sizeof(scenario));if(std::string(scenario)=="crash")ExitProcess(88);if(std::string(scenario)=="hang")Sleep(60000);
    if(position>=10000&&std::string(scenario)!="queue"){Sleep(1);return {};} const auto count=std::string(scenario)=="queue"?4096u:std::min(4096u,10000-position);
    for(unsigned n=0;n<count;++n){auto index=position++;unsigned stream=index%4;if(!config.enabled[stream])continue;Record r;r.id=index+1;r.channel=1+stream/2;r.bus=stream%2;r.source=config.receiveMode==DdcReceiveMode::LegacyMT?2:1;r.timestampNs=(index+100)*2000ull;r.deviceTicks=index+100;r.tickResolutionNs=2000;r.hostMonotonicNs=index*1000ull;r.commandWord=0x1842;r.statusWord=0x1800;r.commandWord2=0x2842;r.statusWord2=0x2800;r.flags=index%7;r.vendorStatus=index%13;r.validity=15;r.messageType=index%5;r.words={std::uint16_t(index),std::uint16_t(index>>16)};batch.records.push_back(std::move(r));}
    for(unsigned c=0;c<2;++c)if(config.enabled[c*2]||config.enabled[c*2+1]){SourceBatch::Packet p;p.channel=c+1;p.chapter10=config.receiveMode!=DdcReceiveMode::LegacyMT;p.bytes.resize(72);for(unsigned i=0;i<p.bytes.size();++i)p.bytes[i]=std::uint8_t(position+c+i);batch.packets.push_back(std::move(p));}return {};
  }
  std::string stopMonitoring()override{check();active=false;return {};}
  std::string drainNext(SourceBatch &,bool &more)override{check();more=false;return {};}
  void close()override{if(owner!=std::thread::id{})check();active=false;}
};
struct Server {
  DdcHelperPack pack;Handle control,data;std::string secret;std::unique_ptr<CaptureSource> source;
  std::atomic_bool closing{false},cancelled{false},transportFailed{false};std::jthread reader,writer,heartbeat;
  std::mutex commandMutex,dataMutex,writeMutex,operationMutex;std::condition_variable commandChanged,dataChanged;std::deque<Frame> commands,dataQueue;std::size_t queued{};
  std::uint64_t controlSequence{},dataSequence{},records{},packets{};std::atomic<std::uint64_t>lastProgress{};std::string currentOperation{"idle"},transportError;bool active{},opened{};SourceBatch finalMetrics;
  void controlSend(Type type,std::uint64_t request,std::vector<std::uint8_t> payload){std::lock_guard lock(writeMutex);writeFrame(control.value,{type,request,++controlSequence,std::move(payload)},2000,&closing);}
  void operation(const std::string &name){std::lock_guard lock(operationMutex);currentOperation=name;}
  CaptureFailure failed(const std::string &detail,const char *stage){auto f=source?source->lastFailure():CaptureFailure{};if(!f){f.category=CaptureFailureCategory::Transport;f.stage=stage;f.operation=currentOperation;f.detail=detail;}if(source&&!source->closeError().empty()){f.cleanupFailed=true;f.recovery.push_back("Device cleanup failed; collect diagnostics before retry");}return f;}
  void reply(const Frame &request,const std::string &error={},std::vector<std::uint8_t> extra={}){Writer w;helper::failure(w,error.empty()?CaptureFailure{}:failed(error,"helper_sdk"));w.text(error);w.number(dataSequence);w.number(records);w.number(packets);w.raw(extra);controlSend(Type::Reply,request.request,std::move(w.bytes));}
  void queue(Type type,std::vector<std::uint8_t> payload){std::lock_guard lock(dataMutex);if(transportFailed)throw std::runtime_error(transportError);auto size=payload.size()+HeaderBytes;if(size>MaxQueuedBytes-queued)throw std::runtime_error("Helper outbound queue exhausted; recording is incomplete");queued+=size;dataQueue.push_back({type,0,++dataSequence,std::move(payload)});dataChanged.notify_one();}
  void publish(const SourceBatch &batch){finalMetrics.hardwareLost=batch.hardwareLost;finalMetrics.hardwareOverflows=batch.hardwareOverflows;finalMetrics.channelLost=batch.channelLost;finalMetrics.channelOverflows=batch.channelOverflows;finalMetrics.pollNs=batch.pollNs;finalMetrics.packetProcessingNs=batch.packetProcessingNs;
    if(batch.records.empty()&&batch.packets.empty())return;
    chunks(batch,[&](SourceBatch part){records+=part.records.size();packets+=part.packets.size();queue(Type::Data,helper::batch(part));});++lastProgress;
  }
  void readCommands(){try{std::uint64_t sequence{};while(!closing){auto frame=readFrame(control.value,sequence,24*60*60*1000u,&closing);if(frame.payload.size()>16384)throw std::runtime_error("Helper command exceeds control bound");if(frame.type==Type::Cancel)cancelled=true;std::lock_guard lock(commandMutex);if(commands.size()>=32)throw std::runtime_error("Helper control queue exhausted");commands.push_back(std::move(frame));commandChanged.notify_one();}}catch(const std::exception &e){if(!closing){std::lock_guard lock(dataMutex);transportError=e.what();transportFailed=true;cancelled=true;commandChanged.notify_all();}}}
  void writeData(){try{while(!closing){Frame frame;{std::unique_lock lock(dataMutex);dataChanged.wait_for(lock,std::chrono::milliseconds(100),[&]{return closing||!dataQueue.empty();});if(closing)break;if(dataQueue.empty())continue;frame=std::move(dataQueue.front());dataQueue.pop_front();queued-=frame.payload.size()+HeaderBytes;}writeFrame(data.value,frame,5000,&closing);}}catch(const std::exception &e){if(!closing){std::lock_guard lock(dataMutex);transportError=e.what();transportFailed=true;cancelled=true;commandChanged.notify_all();}}}
  void health(){try{while(!closing){for(unsigned i=0;i<10&&!closing;++i)Sleep(100);if(closing)break;Writer w;{std::lock_guard lock(operationMutex);w.text(currentOperation);}w.number<std::uint64_t>(lastProgress.load());controlSend(Type::Health,0,std::move(w.bytes));}}catch(...){if(!closing){transportFailed=true;cancelled=true;}}}
  std::string stop(){if(!active)return {};operation("stop_and_drain");auto error=source->stopMonitoring();bool more=true;for(unsigned i=0;more&&i<257;++i){SourceBatch tail;auto message=source->drainNext(tail,more);publish(tail);if(!message.empty()){if(error.empty())error=message;break;}if(i==256&&more&&error.empty())error="Helper drain exceeds bounded SDK pool";}active=false;Writer end;helper::metrics(end,finalMetrics);end.number(records);end.number(packets);queue(Type::End,std::move(end.bytes));return error;}
  int run(){
    reader=std::jthread([&]{readCommands();});writer=std::jthread([&]{writeData();});heartbeat=std::jthread([&]{health();});bool quit=false,authenticated=false;
    try{while(!quit&&!transportFailed){Frame request;bool have=false;{std::unique_lock lock(commandMutex);if(commands.empty()&&!active)commandChanged.wait_for(lock,std::chrono::milliseconds(100));if(!commands.empty()){request=std::move(commands.front());commands.pop_front();have=true;}}
      if(have){
        if(request.type==Type::Hello){Reader r{request.payload};if(r.text()!=secret)throw std::runtime_error("Helper authentication nonce mismatch");r.done();authenticated=true;Writer extra;extra.text(secret);extra.text(pack.helperSha256);extra.text(pack.runtimeSha256);extra.text(pack.id);reply(request,{},std::move(extra.bytes));}
        else {if(!authenticated)throw std::runtime_error("Helper command before authenticated handshake");switch(request.type){
          case Type::Open:{if(opened){reply(request,"Helper already owns an SDK connection");break;}operation("open");Reader r{request.payload};auto mask=r.number<std::uint32_t>();CaptureConfig config;if(!mask||(mask&~15u))throw std::runtime_error("Invalid helper stream mask");for(unsigned i=0;i<4;++i)config.enabled[i]=(mask&(1u<<i))!=0;for(auto &v:config.logicalDevices){v=r.number<std::int32_t>();if(v<0||v>31)throw std::runtime_error("Invalid helper logical device number");}auto mode=r.number<std::uint32_t>(),policy=r.number<std::uint32_t>();if(mode>2||policy>1)throw std::runtime_error("Invalid helper mode/policy");r.done();config.receiveMode=static_cast<DdcReceiveMode>(mode);config.connectionPolicy=static_cast<DdcConnectionPolicy>(policy);if(config.connectionPolicy==DdcConnectionPolicy::QualifiedAutomatic&&!pack.automaticApproved){reply(request,"Selected helper pack has no physical qualification receipt for this PC");break;}config.runtime=pack.runtimePath;config.runtimeSha256=pack.runtimeSha256;config.runtimeArchitecture="x86";config.cancelRequested=[&]{return cancelled.load();};config.diagnostic=[&](const std::string &op,int code,const std::string &detail){Writer w;w.text(op);w.number<std::int32_t>(code);w.text(detail.substr(0,8192));controlSend(Type::Diagnostic,0,std::move(w.bytes));};config.deviceState=[&](unsigned channel,DevicePhase phase,const std::string &detail){Writer w;w.number<std::uint32_t>(channel);w.number<std::uint32_t>(static_cast<unsigned>(phase));w.text(detail.substr(0,8192));controlSend(Type::DeviceState,0,std::move(w.bytes));};cancelled=false;source=pack.fixture?std::unique_ptr<CaptureSource>(std::make_unique<FixtureSource>()):makeVendorDdcSource();auto error=source->open(config);opened=error.empty();reply(request,error);break;}
          case Type::Start:{if(!opened||active){reply(request,"Helper is not idle and connected");break;}operation("start");cancelled=false;records=packets=0;finalMetrics={};auto error=source->startMonitoring();active=error.empty();reply(request,error);break;}
          case Type::Stop:{auto error=stop();reply(request,error);break;}
          case Type::Close:{operation("close");auto error=stop();if(source){source->close();if(!source->closeError().empty())error=source->closeError();}opened=false;reply(request,error);quit=true;break;}
          case Type::Cancel:{cancelled=true;auto error=stop();if(request.request)reply(request,error);break;}
          default:throw std::runtime_error("Unsupported helper command");
        }}
      }
      if(active){operation("poll");SourceBatch batch;auto error=source->poll(batch,{});publish(batch);if(!error.empty())throw std::runtime_error(error);if(batch.hardwareLost||batch.hardwareOverflows)throw std::runtime_error("DDC helper reported raw loss/overflow; recording is incomplete");}
      operation(active?"poll":"idle");
    }
    if(transportFailed)throw std::runtime_error(transportError.empty()?"Helper transport failed":transportError);
    }catch(const std::exception &e){try{if(source){source->stopMonitoring();source->close();}Writer w;helper::failure(w,failed(e.what(),"helper_runtime"));controlSend(Type::Fatal,0,std::move(w.bytes));}catch(...){}quit=true;}
    closing=true;dataChanged.notify_all();commandChanged.notify_all();CancelIoEx(control.value,nullptr);CancelIoEx(data.value,nullptr);if(reader.joinable())reader.join();if(writer.joinable())writer.join();if(heartbeat.joinable())heartbeat.join();return transportFailed?4:0;
  }
};
int inspect(const std::filesystem::path &path){if(!x86Image(path))throw std::runtime_error("Runtime must be x86");Handle module(reinterpret_cast<HANDLE>(LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32)));if(!module)throw winError("Inspect helper runtime");auto h=reinterpret_cast<HMODULE>(module.value);module.value=INVALID_HANDLE_VALUE;struct Free{HMODULE h;~Free(){FreeLibrary(h);}}free{h};for(const char *name:{"aceInitialize","aceGetHwVersionInfo","aceFree","aceGetLibVersion","aceErrorStr","aceSetTimeTagRes"})if(!GetProcAddress(h,name))throw std::runtime_error(std::string("Runtime lacks ")+name);bool mti=true,mt=true;for(const char *name:{"aceMTIConfigure","aceMTIGetCh10DataPkt","aceMTIStart","aceMTIStop","aceMTIFreeCh10DataPkt","aceMTIDecodeRawMsg","aceMtiGetMetrics"})mti&=GetProcAddress(h,name)!=nullptr;for(const char *name:{"aceMTConfigure","aceMTInstallHBuf","aceMTStart","aceMTStop","aceMTGetHBufMsgsRaw","aceMTDecodeRawMsg"})mt&=GetProcAddress(h,name)!=nullptr;if(!mti&&!mt)throw std::runtime_error("Runtime has no complete passive receive interface");std::cout<<"{\"architecture\":\"x86\",\"protocol\":1,\"runtime_sha256\":\""<<hashFile(path)<<"\",\"mti\":"<<(mti?"true":"false")<<",\"legacy_mt\":"<<(mt?"true":"false")<<",\"hardware_opened\":false}\n";return 0;}
}
int wmain(int argc,wchar_t **argv){try{
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
  if(argc==3&&std::wstring(argv[1])==L"--inspect")return inspect(std::filesystem::absolute(argv[2]));
  if(argc!=11)throw std::runtime_error("Use --inspect <x86 runtime> or launch through AeroTrace");
  std::map<std::wstring,std::wstring> args;for(int i=1;i<argc;i+=2)if(!args.emplace(argv[i],argv[i+1]).second)throw std::runtime_error("Duplicate helper argument");
  Server server;server.secret=utf8(args.at(L"--nonce"));if(server.secret.size()!=32)throw std::runtime_error("Invalid helper nonce");const auto parent=std::stoul(args.at(L"--parent"));server.pack=readDdcHelperPack(args.at(L"--pack"));if(!server.pack.error.empty())throw std::runtime_error(server.pack.error);if(std::filesystem::weakly_canonical(executablePath())!=std::filesystem::weakly_canonical(server.pack.helperPath))throw std::runtime_error("Helper pack executable identity differs from running image");if(server.pack.fixture){wchar_t value[4]{};if(GetEnvironmentVariableW(L"AEROTRACE_HELPER_ALLOW_FIXTURES",value,4)!=1)throw std::runtime_error("Fixture helper admission denied");}
  server.control=clientPipe(args.at(L"--control"),5000);server.data=clientPipe(args.at(L"--data"),5000);serverIs(server.control.value,parent);serverIs(server.data.value,parent);return server.run();
}catch(const std::exception &e){std::cerr<<"DDC helper: "<<e.what()<<'\n';return 2;}}
