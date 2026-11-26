#include "local_inference.hpp"
#include <QtConcurrent>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace desktop {
#ifdef Q_OS_WIN
static void resumeChild(DWORD pid) {
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
  if(snapshot==INVALID_HANDLE_VALUE)return;
  THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
  if(Thread32First(snapshot,&entry))do {
    if(entry.th32OwnerProcessID==pid){HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);if(thread){ResumeThread(thread);CloseHandle(thread);}}
  }while(Thread32Next(snapshot,&entry));
  CloseHandle(snapshot);
}
#endif
bool LocalInference::memoryAvailable(quint64 minimum) {
#ifdef Q_OS_WIN
  MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
  return GlobalMemoryStatusEx(&memory) && memory.ullAvailPhys>=minimum;
#else
  return false;
#endif
}
QString LocalInference::assetDirectory() const {
  if(!options.assetsDirectory.isEmpty())return options.assetsDirectory;
  const auto personal=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/assistant-runtime";
  if(QFileInfo::exists(personal+"/manifest.json"))return personal;
  QString root=QCoreApplication::applicationDirPath()+"/assistant-runtime";
  if(!QFileInfo::exists(root+"/manifest.json") && !QFileInfo::exists(QCoreApplication::applicationDirPath()+"/deployment.json"))root=QString::fromUtf8(AEROTRACE_ASSISTANT_ASSETS);
  return root;
}
bool LocalInference::assetsPresent() const {
  const auto root=assetDirectory();
  return QFileInfo::exists(root+"/manifest.json")&&QFileInfo::exists(root+"/runtime/llama-server.exe");
}
bool LocalInference::selectAssets(const QString &directory) {
  if(busy||!QFileInfo::exists(directory+"/manifest.json")||
     !QFileInfo::exists(directory+"/runtime/llama-server.exe"))return false;
  const auto root=QDir(directory).canonicalPath();
  if(root.isEmpty())return false;
  if(root!=options.assetsDirectory){stop();options.assetsDirectory=root;}
  return true;
}
bool LocalInference::hasMemory(quint64 minimum) const {return options.availableMemory?options.availableMemory(minimum):memoryAvailable(minimum);}
LocalInference::LocalInference(QObject *parent):LocalInference(parent,Options{}){}
LocalInference::LocalInference(QObject *parent,Options configuration):QObject(parent),options(std::move(configuration)) {
  network.setProxy(QNetworkProxy::NoProxy);
  idle.setSingleShot(true);idle.setTimerType(Qt::PreciseTimer);idle.setInterval(120000);
  connect(&idle,&QTimer::timeout,this,[this]{stop();});
  pressure.setInterval(2000);
  connect(&pressure,&QTimer::timeout,this,[this]{if(!hasMemory(1024ull*1024*1024)){finish("Model unloaded because available memory is low. Structured evidence remains available.");stop();}});
  health.setInterval(300);
  connect(&health,&QTimer::timeout,this,[this]{
    if(startup.elapsed()>90000){finish("Offline model startup timed out. Showing source evidence.");stop();return;}
    if(checking)return;
    checking=true;const auto healthGeneration=generation;
    QNetworkRequest request(QUrl(endpoint+"/health"));request.setTransferTimeout(2000);
    request.setRawHeader("Authorization",("Bearer "+token).toUtf8());
    auto reply=network.get(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply,healthGeneration]{
      if(healthGeneration!=generation){reply->deleteLater();return;}
      checking=false;const bool ok=reply->error()==QNetworkReply::NoError;reply->deleteLater();
      if(ok && pending && !stopping){readyState=true;health.stop();state="AeroTrace Assistant · CPU · 2 threads · 2048 tokens";finish();}
    });
  });
  connect(&server,&QProcess::readyReadStandardError,this,[this]{log+=server.readAllStandardError();log=log.right(8192);});
  connect(&server,&QProcess::readyReadStandardOutput,this,[this]{server.readAllStandardOutput();});
  connect(&server,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){finish("Offline model process could not start. Showing source evidence.");stop();}});
  connect(&server,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){readyState=false;stopping=false;health.stop();pressure.stop();finish("Offline model stopped or reached its resource limit. Showing source evidence.");state="Offline model unloaded";resourceJob.reset();});
}
LocalInference::~LocalInference(){pending={};stop();server.waitForFinished(1500);}
void LocalInference::finish(QString error) {
  if(!error.isEmpty()){busy=false;state=error;}
  if(!pending)return;
  auto callback=std::move(pending);pending={};
  callback(error.isEmpty()?endpoint:QString(),error.isEmpty()?token:QString(),error);
}
void LocalInference::stop(){++generation;readyState=false;stopping=server.state()!=QProcess::NotRunning;health.stop();idle.stop();pressure.stop();if(server.state()!=QProcess::NotRunning)server.kill();state="Offline model unloaded";key.close();if(!key.fileName().isEmpty())key.remove();}
void LocalInference::cancel(){finish("Model request cancelled. Structured evidence remains available.");busy=false;stop();}
void LocalInference::pauseForCapture(){if(busy || server.state()!=QProcess::NotRunning){finish("Model generation paused because capture started. Showing source evidence.");busy=false;stop();}}
void LocalInference::release(){busy=false;if(server.state()==QProcess::Running)idle.start();}
void LocalInference::acquire(bool acquisitionActive,Ready ready) {
  if(busy){ready({}, {},"One model request is already running.");return;}
  if(acquisitionActive){ready({}, {},"Model generation is paused while capture is running. Structured evidence remains available.");return;}
  if(!hasMemory(readyState?1024ull*1024*1024:3ull*1024*1024*1024)){ready({}, {},"Insufficient available memory for model generation (3 GiB required for initial loading). Structured evidence remains available.");return;}
  if(stopping){ready({}, {},"Offline model is shutting down. Showing source evidence.");return;}
  busy=true;idle.stop();pending=std::move(ready);
  if(server.state()==QProcess::Running && readyState){finish();return;}
  state="Verifying offline model and runtime…";
  const auto requestGeneration=++generation;
  auto watcher=new QFutureWatcher<QString>(this);
  connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,requestGeneration]{const auto error=watcher->result();watcher->deleteLater();if(!pending || requestGeneration!=generation)return;if(!error.isEmpty()){finish(error);return;}start();});
  watcher->setFuture(QtConcurrent::run([root=assetDirectory()]{
    QFile manifest(root+"/manifest.json");
    if(!manifest.open(QIODevice::ReadOnly))return QString("Offline model bundle is missing. Showing source evidence.");
    const auto data=QJsonDocument::fromJson(manifest.readAll()).object();
    const auto model=data["model"].toObject();
    if(model["sha256"].toString()!="d2387ca2dbfee2ffabce7120d3770dadca0b293052bc2f0e138fdc940d9bc7b5")return QString("Offline model identity does not match the release pin.");
    auto files=data["files"].toObject();files[model["file"].toString()]=model["sha256"];
    for(auto it=files.begin();it!=files.end();++it){
      if(it.key().contains("..") || QDir::isAbsolutePath(it.key()))return QString("Invalid model bundle path.");
      QFile file(root+"/"+it.key());QCryptographicHash hash(QCryptographicHash::Sha256);
      if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file) || QString::fromLatin1(hash.result().toHex())!=it.value().toString().toLower())return QString("Offline model bundle hash verification failed: ")+it.key();
    }
    return QString();
  }));
}
void LocalInference::start() {
  const auto root=assetDirectory();
  QTcpServer reservation;
  if(!reservation.listen(QHostAddress::LocalHost,0)){finish("Cannot reserve a local model port.");return;}
  const auto port=reservation.serverPort();reservation.close();
  endpoint=QString("http://127.0.0.1:%1").arg(port);
  token=QUuid::createUuid().toString(QUuid::Id128)+QUuid::createUuid().toString(QUuid::Id128);
  if(!key.open() || key.write(token.toUtf8()+"\n")<0 || !key.flush()){finish("Cannot create the private model authentication key.");return;}
  key.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
#ifdef Q_OS_WIN
  resourceJob=std::shared_ptr<void>(CreateJobObjectW(nullptr,nullptr),[](void *handle){if(handle)CloseHandle(handle);});
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
  limits.ProcessMemoryLimit=2560ull*1024*1024;
  JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};cpu.ControlFlags=JOB_OBJECT_CPU_RATE_CONTROL_ENABLE|JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;cpu.CpuRate=2000;
  if(!resourceJob || !SetInformationJobObject(resourceJob.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)) || !SetInformationJobObject(resourceJob.get(),JobObjectCpuRateControlInformation,&cpu,sizeof(cpu))){finish("Cannot enforce offline model resource limits.");stop();return;}
  server.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *arguments){arguments->flags|=CREATE_SUSPENDED|CREATE_NO_WINDOW;});
  // Assign before resuming the primary thread, so startup allocations are bounded.
  QObject::disconnect(&server,&QProcess::started,this,nullptr);
  connect(&server,&QProcess::started,this,[this]{
    HANDLE process=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE|PROCESS_SET_INFORMATION,FALSE,DWORD(server.processId()));
    const bool assigned=process && AssignProcessToJobObject(resourceJob.get(),process);
    if(process)CloseHandle(process);
    // Qt does not expose the primary thread handle. Resume each thread belonging
    // to this new process through a bounded Toolhelp snapshot.
    if(!assigned){finish("Cannot isolate offline inference.");stop();return;}
    resumeChild(DWORD(server.processId()));
  });
#endif
  auto environment=QProcessEnvironment::systemEnvironment();
  for(const auto &name:environment.keys())if(name.startsWith("LLAMA_"))environment.remove(name);
  environment.insert("OMP_NUM_THREADS","2");server.setProcessEnvironment(environment);
  state="Loading offline assistant…";checking=false;startup.start();log.clear();
  server.setWorkingDirectory(root+"/runtime");
  server.start(options.program.isEmpty()?root+"/runtime/llama-server.exe":options.program,{"--model",root+"/Qwen3-1.7B-Q4_K_M.gguf","--host","127.0.0.1","--port",QString::number(port),"--api-key-file",key.fileName(),"--alias","aerotrace-icd","--ctx-size","2048","--threads","2","--threads-batch","2","--threads-http","1","--parallel","1","--n-gpu-layers","0","--offline","--no-webui","--no-warmup"});
  health.start();pressure.start();
}
}
