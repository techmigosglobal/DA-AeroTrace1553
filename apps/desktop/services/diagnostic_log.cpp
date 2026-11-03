#include "diagnostic_log.hpp"
#include <cstdio>
#include <atomic>
#include <thread>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif
namespace {
struct Journal {
  QMutex mutex;QWaitCondition wake,drained;QString directory,path,run,error;QElapsedTimer clock;
  quint64 sequence{};unsigned segment{};std::atomic<bool> initialized{};
  QQueue<QByteArray> pending;qsizetype pendingBytes{};bool writing{},stopping{};std::thread worker;
};
Journal &journal(){static auto *value=new Journal;return *value;}
const char *severityName(QtMsgType type) {
  switch(type){case QtDebugMsg:return "debug";case QtInfoMsg:return "info";case QtWarningMsg:return "warning";case QtCriticalMsg:return "error";case QtFatalMsg:return "fatal";}
  return "unknown";
}
QByteArray record(Journal &j,const QString &event,const QString &detail,const QJsonObject &fields,QtMsgType severity=QtInfoMsg) {
  return QJsonDocument(QJsonObject{{"schema","aerotrace.log.v1"},{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    {"elapsed_ms",j.clock.elapsed()},{"run_id",j.run},{"pid",QCoreApplication::applicationPid()},
    {"thread",QString::number(quintptr(QThread::currentThreadId()),16)},{"sequence",QString::number(++j.sequence)},
    {"event",event},{"severity",severityName(severity)},{"detail",detail},{"fields",fields}}).toJson(QJsonDocument::Compact)+'\n';
}
void writerLoop() {
  auto &j=journal();
  for(;;) {
    QList<QByteArray> batch;QString path;
    {QMutexLocker lock(&j.mutex);while(j.pending.isEmpty()&&!j.stopping)j.wake.wait(&j.mutex,100);
      if(j.pending.isEmpty()&&j.stopping)break;
      while(!j.pending.isEmpty()){batch.append(j.pending.dequeue());j.pendingBytes-=batch.last().size();}j.writing=true;path=j.path;}
    QString error;
    QByteArray chunk;
    const auto publish=[&] {
      if(chunk.isEmpty())return true;
      if(QFileInfo(path).size()>=8*1024*1024){QMutexLocker lock(&j.mutex);++j.segment;path=j.directory+"/application-"+j.run+QString("-%1.jsonl").arg(j.segment,3,10,QChar('0'));j.path=path;}
      QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Append)||file.write(chunk)!=chunk.size()||!file.flush()){error="Cannot write diagnostic journal "+path+": "+file.errorString();return false;}chunk.clear();return true;
    };
    for(const auto &bytes:batch){chunk+=bytes;if(chunk.size()>=256*1024&&!publish())break;}
    if(error.isEmpty())publish();
    {QMutexLocker lock(&j.mutex);j.writing=false;if(!error.isEmpty()){j.error=error;std::fprintf(stderr,"AeroTrace diagnostic write failed\n");}if(j.pending.isEmpty())j.drained.wakeAll();}
  }
}
#ifdef Q_OS_WIN
wchar_t crashDumpPath[32768]{},crashTextPath[32768]{};
LONG WINAPI crashFilter(EXCEPTION_POINTERS *info){writeApplicationCrashDump(info);return EXCEPTION_EXECUTE_HANDLER;}
#endif
}
bool initializeApplicationLogging(const QString &directory) {
  auto &j=journal();QMutexLocker lock(&j.mutex);if(j.initialized)return j.error.isEmpty();
  j.directory=directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Logs":QDir(directory).absolutePath();
  j.run=QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzzZ")+"-"+QString::number(QCoreApplication::applicationPid())+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8);
  j.path=j.directory+"/application-"+j.run+"-000.jsonl";j.clock.start();j.initialized=true;
  if(!QDir().mkpath(j.directory)){j.error="Cannot create log directory: "+j.directory;return false;}
  j.pending.enqueue(record(j,"application_start","",{{"version",QCoreApplication::applicationVersion()},{"executable",QCoreApplication::applicationFilePath()},{"qt",QString(qVersion())},{"os",QSysInfo::prettyProductName()},{"architecture",QSysInfo::currentCpuArchitecture()},{"arguments",QJsonArray::fromStringList(QCoreApplication::arguments())}}));j.pendingBytes=j.pending.head().size();
  j.worker=std::thread(writerLoop);std::atexit(shutdownApplicationLogging);return true;
}
bool applicationEvent(const QString &event,const QString &detail,const QJsonObject &fields,QtMsgType severity) {
  if(!journal().initialized && !initializeApplicationLogging())return false;
  auto &j=journal();QMutexLocker lock(&j.mutex);
  if(j.stopping||!j.worker.joinable()||!j.error.isEmpty())return false;
  if(severity==QtInfoMsg && (event.endsWith("_error")||event.endsWith("_failed")||event=="unhandled_exception"||
      !fields.value("error").toString().isEmpty()||fields.value("failed").toBool()))severity=QtCriticalMsg;
  const auto bytes=record(j,event,detail,fields,severity);
  if(j.pending.size()>=8192 || j.pendingBytes+bytes.size()>16*1024*1024){j.error="Diagnostic queue capacity exceeded; logging is incomplete. Collect diagnostics and restart.";return false;}
  j.pending.enqueue(bytes);j.pendingBytes+=bytes.size();j.wake.wakeOne();return true;
}
void installApplicationQtLogging() {
  qInstallMessageHandler([](QtMsgType type,const QMessageLogContext &context,const QString &message){
    const bool saved=applicationEvent("qt_message",message,{{"level",int(type)},
      {"category",QString::fromUtf8(context.category?context.category:"")},
      {"file",QString::fromUtf8(context.file?context.file:"")},
      {"function",QString::fromUtf8(context.function?context.function:"")},{"line",context.line}},type);
    // Avoid recursively invoking Qt logging on a failed sink.
    if(!saved)std::fprintf(stderr,"AeroTrace log unavailable [%s]: %s\n",severityName(type),message.toUtf8().constData());
    if(type==QtFatalMsg){flushApplicationLogging();writeApplicationCrashDump();}
  });
}
bool flushApplicationLogging() {auto &j=journal();QMutexLocker lock(&j.mutex);while((!j.pending.isEmpty()||j.writing)&&j.worker.joinable())j.drained.wait(&j.mutex);return j.error.isEmpty();}
void shutdownApplicationLogging() {auto &j=journal();{QMutexLocker lock(&j.mutex);j.stopping=true;j.wake.wakeAll();}if(j.worker.joinable())j.worker.join();}
QString applicationLogDirectory(){auto &j=journal();QMutexLocker lock(&j.mutex);return j.directory;}
QString applicationLogPath(){auto &j=journal();QMutexLocker lock(&j.mutex);return j.path;}
QString applicationRunId(){auto &j=journal();QMutexLocker lock(&j.mutex);return j.run;}
QString applicationLogError(){auto &j=journal();QMutexLocker lock(&j.mutex);return j.error;}
void installApplicationCrashHandler() {
#ifdef Q_OS_WIN
  const auto prefix=applicationLogDirectory()+"/application-"+applicationRunId();
  if(prefix.size()+16>=32768)return;
  (prefix+".crash.dmp").toWCharArray(crashDumpPath);(prefix+".crash.txt").toWCharArray(crashTextPath);SetUnhandledExceptionFilter(crashFilter);
#endif
}
void writeApplicationCrashDump(void *exceptionPointers) noexcept {
#ifdef Q_OS_WIN
  if(!crashDumpPath[0])return;
  HANDLE text=CreateFileW(crashTextPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(text!=INVALID_HANDLE_VALUE){const char message[]="Unhandled application failure. Preserve this run's journals and capture sidecars. A best-effort minidump is recorded beside this file.\r\n";DWORD written;WriteFile(text,message,sizeof(message)-1,&written,nullptr);FlushFileBuffers(text);CloseHandle(text);}
  HANDLE file=CreateFileW(crashDumpPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return;
  MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),static_cast<EXCEPTION_POINTERS *>(exceptionPointers),FALSE};
  MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,exceptionPointers?&info:nullptr,nullptr,nullptr);FlushFileBuffers(file);CloseHandle(file);
#else
  Q_UNUSED(exceptionPointers);
#endif
}
