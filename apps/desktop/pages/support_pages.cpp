#include "main_window.hpp"
void Window::collectDiagnostics() {
  for(auto process:findChildren<QProcess *>())if(process->property("supportCollector").toBool() && process->state()!=QProcess::NotRunning){toast("Support collection is already running");return;}
  QString script=QCoreApplication::applicationDirPath()+"/scripts/collect-diagnostics.ps1";
  if(!QFileInfo::exists(script))script=QString::fromUtf8(AEROTRACE_DIAGNOSTIC_COLLECTOR);
  if(!QFileInfo::exists(script)){toast("Support collector is missing: "+script);return;}
  const auto output=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/AeroTraceDiagnostics/"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz")+"-"+applicationRunId().right(8);
  auto process=new QProcess(this);process->setProperty("supportCollector",true);process->setProcessChannelMode(QProcess::MergedChannels);
  applicationEvent("support_collection_start",output,{{"capture",currentFile},{"recordings",sessionDir},{"capture_active",backend.acquisition.active()}});
  connect(process,&QProcess::finished,this,[this,process,output](int code,QProcess::ExitStatus status){applicationEvent("support_collection_finished",QString::fromUtf8(process->readAll()),{{"exit_code",code},{"exit_status",int(status)},{"output",output}});toast(code==0?"Support bundle saved: "+output:"Support collection failed; see application logs");if(code==0)QDesktopServices::openUrl(QUrl::fromLocalFile(output));process->deleteLater();});
  connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError){applicationEvent("support_collection_error",process->errorString());toast(process->errorString());if(process->state()==QProcess::NotRunning)process->deleteLater();});
  auto timeout=new QTimer(process);timeout->setSingleShot(true);connect(timeout,&QTimer::timeout,process,[process]{applicationEvent("support_collection_timeout");process->kill();});timeout->start(120000);
  const auto shell=qEnvironmentVariable("WINDIR","C:/Windows")+"/System32/WindowsPowerShell/v1.0/powershell.exe";
  QStringList arguments={"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",script,"-ApplicationDirectory",QCoreApplication::applicationDirPath(),"-OutputDirectory",output,"-DataDirectory",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),"-SessionDirectory",sessionDir,"-PreferencesPath",settings.fileName()};
  if(!currentFile.isEmpty())arguments<<"-CapturePath"<<currentFile;
  if(!backend.captureConfig.runtime.empty())arguments<<"-RuntimePath"<<QString::fromStdWString(backend.captureConfig.runtime.wstring());
  process->start(shell,arguments);
  toast("Collecting logs, capture sidecars and driver inventory…");
}
