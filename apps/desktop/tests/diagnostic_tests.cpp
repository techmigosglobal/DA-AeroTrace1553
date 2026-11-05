#include "services/diagnostic_log.hpp"
#include <QtCore>
#include <thread>
#include <vector>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char **argv){QCoreApplication app(argc,argv);app.setApplicationName("AeroTraceDiagnosticsTests");const auto args=app.arguments();
  if(args.contains("--bad-dir")){QTemporaryDir temp;QFile blocker(temp.path()+"/blocker");if(!blocker.open(QIODevice::WriteOnly))return 2;blocker.close();return !initializeApplicationLogging(blocker.fileName()+"/child") && !applicationLogError().isEmpty()?0:1;}
#ifdef Q_OS_WIN
  if(args.contains("--crash-child")){if(!initializeApplicationLogging(args.last()))return 2;installApplicationCrashHandler();SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);RaiseException(0xE000A301,0,0,nullptr);return 3;}
#endif
  try{QTemporaryDir temp;CHECK(initializeApplicationLogging(temp.path()+"/logs"));const auto run=applicationRunId();CHECK(!run.isEmpty());
    std::vector<std::thread> workers;for(int thread=0;thread<4;++thread)workers.emplace_back([thread]{for(int i=0;i<100;++i)CHECK(applicationEvent("concurrent","escaped\n\"",{{"worker",thread},{"item",i}}));});for(auto &worker:workers)worker.join();
    CHECK(flushApplicationLogging());QFile log(applicationLogPath());CHECK(log.open(QIODevice::ReadOnly));quint64 sequence=0;int count=0;while(!log.atEnd()){QJsonParseError error;const auto o=QJsonDocument::fromJson(log.readLine(),&error).object();CHECK(error.error==QJsonParseError::NoError);CHECK(o["run_id"]==run && o["sequence"].toString().toULongLong()==++sequence);CHECK(!o["utc"].toString().isEmpty());++count;}CHECK(count==401);log.close();
    installApplicationQtLogging();
    QMessageLogger("qualification.cpp",155,"receiveBatch","aerotrace.capture").warning("Test driver warning");
    QMessageLogger("qualification.cpp",156,"receiveBatch","aerotrace.capture").critical("Test SDK failure");
    CHECK(flushApplicationLogging());CHECK(log.open(QIODevice::ReadOnly));
    int qtMessages=0;while(!log.atEnd()){const auto row=QJsonDocument::fromJson(log.readLine()).object();if(row["event"]!="qt_message")continue;
      CHECK(row["severity"]==(qtMessages?"error":"warning"));CHECK(row["fields"].toObject()["category"]=="aerotrace.capture");
      CHECK(row["fields"].toObject()["function"]=="receiveBatch");CHECK(row["fields"].toObject()["line"]==155+qtMessages);++qtMessages;}
    CHECK(qtMessages==2);log.close();
    CHECK(applicationEvent("rotation",QString(8*1024*1024,'x')));CHECK(applicationEvent("after_rotation"));CHECK(flushApplicationLogging());CHECK(QDir(applicationLogDirectory()).entryList({"*.jsonl"},QDir::Files).size()==2);
    QProcess negative;negative.start(app.applicationFilePath(),{"--bad-dir"});CHECK(negative.waitForFinished(15000) && negative.exitCode()==0);
#ifdef Q_OS_WIN
    const auto crash=temp.path()+"/crash";QProcess child;child.start(app.applicationFilePath(),{"--crash-child",crash});CHECK(child.waitForFinished(15000));const auto dumps=QDir(crash).entryList({"*.dmp"},QDir::Files);CHECK(dumps.size()==1 && QFileInfo(crash+"/"+dumps[0]).size()>32);CHECK(QDir(crash).entryList({"*.crash.txt"},QDir::Files).size()==1);
#endif
    std::cout<<"PASS: concurrent structured journals, sequence, escaping, rotation, failed storage and isolated crash dump\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
