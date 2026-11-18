#pragma once
#include <QtCore>
#include <functional>

namespace desktop {
// Small library operations only. Extraction/inference keep their existing
// separately budgeted worker/process owners.
inline void libraryJob(QObject *owner,QJsonObject request,std::function<void(QJsonObject,QString)> done) {
  const auto directory=QCoreApplication::applicationDirPath();
  const bool deployed=QFileInfo::exists(directory+"/deployment.json");
  QString python=directory+"/document-runtime/python.exe",script=directory+"/document-worker/worker.py";
  if(!deployed && !QFileInfo::exists(python))python=QDir(qEnvironmentVariable("ProgramFiles","C:/Program Files")).filePath("AeroTrace1553/document-runtime/python.exe");
  if(!deployed && !QFileInfo::exists(python))python=QString::fromUtf8(AEROTRACE_DOCUMENT_PYTHON);
  if(!deployed && !QFileInfo::exists(script))script=QString::fromUtf8(AEROTRACE_DOCUMENT_WORKER);
  if(!QFileInfo::exists(python)||!QFileInfo::exists(script)){done({},"Document runtime is unavailable");return;}
  request["store"]=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents";
  auto process=new QProcess(owner);auto buffer=std::make_shared<QByteArray>();auto result=std::make_shared<QJsonObject>();auto error=std::make_shared<QString>();
  auto environment=QProcessEnvironment::systemEnvironment();environment.remove("PYTHONPATH");environment.remove("PYTHONHOME");environment.insert("PYTHONDONTWRITEBYTECODE","1");environment.insert("PYTHONNOUSERSITE","1");environment.insert("PYTHONIOENCODING","utf-8");process->setProcessEnvironment(environment);
  QObject::connect(process,&QProcess::started,owner,[process,request]{process->write(QJsonDocument(request).toJson(QJsonDocument::Compact)+'\n');process->closeWriteChannel();});
  QObject::connect(process,&QProcess::readyReadStandardOutput,owner,[process,buffer,result,error]{*buffer+=process->readAllStandardOutput();if(buffer->size()>4*1024*1024){*error="Library response exceeded its limit";process->kill();return;}while(buffer->contains('\n')){auto end=buffer->indexOf('\n');auto object=QJsonDocument::fromJson(buffer->left(end)).object();buffer->remove(0,end+1);if(object["event"]=="done")*result=object;else if(object["event"]=="error")*error=object["message"].toString();}});
  auto completed=std::make_shared<bool>(false);
  auto finish=[process,result,error,done,completed](int code){if(*completed)return;*completed=true;done(*result,!error->isEmpty()?*error:code==0&&!result->isEmpty()?QString():"Library operation did not complete");process->deleteLater();};
  QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,[finish](int code,QProcess::ExitStatus){finish(code);});
  QObject::connect(process,&QProcess::errorOccurred,owner,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(-1);});
  QTimer::singleShot(30000,process,[process,error]{if(process->state()!=QProcess::NotRunning){*error="Library operation timed out";process->kill();}});
  process->start(python,{script});
}
}
