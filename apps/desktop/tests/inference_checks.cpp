#include "services/local_inference.hpp"
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
bool waitFor(std::function<bool()> ready,int timeout=100000) {
  QElapsedTimer clock;clock.start();
  while(!ready() && clock.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(10);}
  return ready();
}
}
int main(int argc,char **argv) {
  QCoreApplication app(argc,argv);
  QStringList receipt;bool passed=true;
  const auto check=[&](bool ok,QString label){receipt<<(ok?"PASS: ":"FAIL: ")+label;passed&=ok;};
  desktop::LocalInference::Options low;low.availableMemory=[](quint64){return false;};
  desktop::LocalInference constrained(nullptr,low);
  QString reason;bool ready=false;
  constrained.acquire(false,[&](QString,QString,QString error){reason=error;ready=true;});
  check(ready && reason.contains("memory") && !constrained.processId(),"Low memory keeps evidence available without starting inference");
  ready=false;constrained.acquire(true,[&](QString,QString,QString error){reason=error;ready=true;});
  check(ready && reason.contains("capture") && !constrained.processId(),"Capture pressure prevents model startup");
  QTemporaryDir missing;desktop::LocalInference::Options absent;absent.assetsDirectory=missing.path();absent.availableMemory=[](quint64){return true;};
  desktop::LocalInference missingModel(nullptr,absent);ready=false;
  missingModel.acquire(false,[&](QString,QString,QString error){reason=error;ready=true;});
  check(waitFor([&]{return ready;}) && reason.contains("missing"),"Missing bundle fails to source evidence");
  desktop::LocalInference::Options failed;failed.program=missing.path()+"/missing-server.exe";failed.availableMemory=[](quint64){return true;};
  desktop::LocalInference failedServer(nullptr,failed);ready=false;
  failedServer.acquire(false,[&](QString,QString,QString error){reason=error;ready=true;});
  check(waitFor([&]{return ready;}) && reason.contains("could not start"),"Failed server startup returns a recoverable evidence-only answer");
  if(app.arguments().contains("--qualification")) {
    bool available=true;
    desktop::LocalInference::Options target;target.availableMemory=[&](quint64){return available;};
    desktop::LocalInference model(nullptr,target);
    QElapsedTimer clock;clock.start();ready=false;QString endpoint,token;
    model.acquire(false,[&](QString url,QString key,QString error){endpoint=url;token=key;reason=error;ready=true;});
    bool second=false;model.acquire(false,[&](QString,QString,QString error){second=error.contains("already");});
    check(second,"Only one inference request can own the model");
    check(waitFor([&]{return ready;}) && reason.isEmpty() && model.processId(),"Hash-verified CPU model starts in its managed process");
    receipt<<QString("Cold startup (hash verification included): %1 ms").arg(clock.elapsed());
    if(!endpoint.isEmpty()) {
      QNetworkAccessManager network;
      QNetworkRequest noKey(QUrl(endpoint+"/v1/models"));auto denied=network.get(noKey);
      check(waitFor([&]{return denied->isFinished();},10000) && denied->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==401,"Loopback model endpoint rejects requests without authentication");denied->deleteLater();
      for(int run=0;run<2;++run){
        QNetworkRequest request(QUrl(endpoint+"/v1/chat/completions"));request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");request.setRawHeader("Authorization",("Bearer "+token).toUtf8());request.setTransferTimeout(90000);
        const QJsonObject body{{"model","aerotrace-icd"},{"messages",QJsonArray{QJsonObject{{"role","system"},{"content","Explain only this evidence in one short sentence with citation [1]: RT4 SA2 transmits 32 words [1]."}},QJsonObject{{"role","user"},{"content","What is the message route?"}}}},{"max_tokens",64},{"temperature",0},{"chat_template_kwargs",QJsonObject{{"enable_thinking",false}}}};
        clock.restart();auto reply=network.post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));
        const bool completed=waitFor([&]{return reply->isFinished();},95000);const auto result=QJsonDocument::fromJson(reply->readAll()).object();
        check(completed && reply->error()==QNetworkReply::NoError && !result["choices"].toArray().empty(),QString("%1 bounded explanatory response").arg(run?"Warm":"Cold"));
        receipt<<QString("%1 response: %2 ms · %3").arg(run?"Warm":"Cold").arg(clock.elapsed()).arg(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));reply->deleteLater();
      }
#ifdef Q_OS_WIN
      HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,DWORD(model.processId()));
      PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
      if(process && GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),sizeof(memory)))receipt<<QString("Peak working set: %1 MiB; private committed: %2 MiB").arg(memory.PeakWorkingSetSize/(1024*1024)).arg(memory.PrivateUsage/(1024*1024));
      if(process)CloseHandle(process);
#endif
      model.release();ready=false;clock.restart();model.acquire(false,[&](QString url,QString,QString error){ready=!url.isEmpty() && error.isEmpty();});
      check(ready && clock.elapsed()<1000,"Warm model is reused without loading another process");
      model.cancel();check(waitFor([&]{return !model.processId();},10000),"Cancellation terminates managed inference");
      ready=false;model.acquire(false,[&](QString url,QString,QString error){ready=!url.isEmpty() && error.isEmpty();});
      check(waitFor([&]{return ready;}) && model.processId(),"Model restarts after cancellation");
      available=false;check(waitFor([&]{return !model.processId();},10000),"Memory pressure unloads the model");available=true;
      ready=false;model.acquire(false,[&](QString url,QString,QString error){ready=!url.isEmpty() && error.isEmpty();});
      check(waitFor([&]{return ready;}),"Model returns after pressure clears");model.pauseForCapture();
      check(waitFor([&]{return !model.processId();},10000),"Starting acquisition unloads a running model");
      ready=false;model.acquire(false,[&](QString url,QString,QString error){ready=!url.isEmpty() && error.isEmpty();});
      check(waitFor([&]{return ready;}),"Model returns after acquisition pressure clears");model.release();clock.restart();
      check(waitFor([&]{return !model.processId();},125000) && clock.elapsed()>=119000,"Model unloads after two idle minutes");receipt<<QString("Idle unload observed: %1 ms").arg(clock.elapsed());
    }
  }
  const auto index=app.arguments().indexOf("--report");
  if(index>=0){QFile output(app.arguments().value(index+1));if(output.open(QIODevice::WriteOnly))output.write(receipt.join('\n').toUtf8());else passed=false;}
  std::cout<<receipt.join('\n').toStdString()<<std::endl;
  return passed?0:1;
}
