#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#endif

void Window::setupOperatorShell() {
  findChild<QLabel *>("brandIcon")->setPixmap(QIcon(":/icons/aerotrace.ico").pixmap(30,30));
  applicationEvent("workspace_started",backend.captureConfig.hardware ? "DDC receive-only source selected" : "Explicit simulator source selected");
  setAcceptDrops(true);configurationRetry.setSingleShot(true);configurationRetry.setInterval(800);
  connect(&configurationRetry,&QTimer::timeout,this,[this]{automaticCapture();});
  sidebar=findChild<QFrame *>("sidebar");
  sidebarToggle=findChild<QPushButton *>("sidebarToggle");
  captureButton=findChild<QPushButton *>("globalCaptureButton");
  const QStringList icons={"shield","home","settings","dashboard","activity","search","sliders","chart","grid","bell","folder","play","file","edit","download","shield","settings","help"};
  for(int i=0;i<nav.size();++i) {
    nav[i]->setProperty("fullText",nav[i]->text());
    nav[i]->setIcon(QIcon(":/icons/"+icons[i]+".svg"));nav[i]->setIconSize(QSize(21,21));nav[i]->setProperty("navigationIcon",nav[i]->icon());
    nav[i]->setAccessibleName(names[i]);nav[i]->setToolTip(names[i]);
  }
  findChild<QLabel *>("brandText")->setStyleSheet("font-size:16px;font-weight:700;");
  findChild<QLabel *>("platformCaption")->hide();licenseBadge->hide();footerBadge->hide();
  auto preferences=findChild<QPushButton *>("preferencesButton");
  auto chat=findChild<QPushButton *>("icdChatButton");chat->setIcon(QIcon(":/icons/file.svg"));chat->setToolTip("ICD Assistant · Ctrl+J");
  connect(chat,&QPushButton::clicked,this,[this]{if(unlocked) documentCenter();});
  shortcut(QKeySequence("Ctrl+J"),[this]{if(unlocked) documentCenter();});
  preferences->setIcon(QIcon(":/icons/settings.svg"));preferences->setAccessibleName("Preferences");preferences->setToolTip("Preferences");
  sidebarToggle->setAccessibleName("Collapse navigation");sidebarToggle->setToolTip("Collapse navigation · Ctrl+B");
  connect(sidebarToggle,&QPushButton::clicked,this,[this]{setSidebarCollapsed(!sidebarCollapsed);});
  auto more=findChild<QPushButton *>("moreTools");more->setIcon(QIcon(":/icons/more.svg"));more->setToolTip("All tools");more->setAccessibleName("More tools");
  connect(more,&QPushButton::clicked,this,[this,more] {
    QMenu menu(this);
    auto tasks=menu.addAction("Tasks and automation…");connect(tasks,&QAction::triggered,this,[this]{if(unlocked)taskCenter();});menu.addSeparator();
    for(int i: {5,8,9,11,13,14,15,17,0,16}) {
      auto action=menu.addAction(nav[i]->icon(),names[i]);
      connect(action,&QAction::triggered,this,[this,i]{navigate(i);});
    }
    menu.exec(more->mapToGlobal(QPoint(more->width(),0)));
  });
  shortcut(QKeySequence("Ctrl+Shift+T"),[this]{if(unlocked)taskCenter();});
  for(int i: {5,8,9,11,13,14,15,17,0,16}) nav[i]->hide();
  connect(captureButton,&QPushButton::clicked,this,[this] {
    if(connecting) {automaticHeld=true;backend.acquisition.requestStop();}
    else if(backend.acquisition.connected() || backend.acquisition.active()) disconnectDevices();
    else if(backend.captureConfig.hardware) connectDevices();
    else startCapture();
  });
  setSidebarCollapsed(settings.value("sidebarCollapsed",false).toBool());
  shortcut(QKeySequence("Ctrl+B"),[this]{setSidebarCollapsed(!sidebarCollapsed);});
  shortcut(QKeySequence("Ctrl+1"),[this]{navigate(4);});
  shortcut(QKeySequence("Ctrl+2"),[this]{navigate(6);});
  shortcut(QKeySequence("Ctrl+3"),[this]{navigate(7);});
  shortcut(QKeySequence("Ctrl+I"),[this]{if(unlocked) importIcd();});
  nav[12]->setToolTip("ICD library · Ctrl+I imports a PDF or profile. You can also drop the file onto the workspace.");
  if(!QStandardPaths::isTestModeEnabled() && settings.value("operatorWorkflowVersion",0).toInt()<2) {
    for(int ch=1;ch<=2;++ch) for(const auto &bus:{"A","B"}) settings.setValue(QString("channel%1Bus %2").arg(ch).arg(bus),true);
    settings.setValue("operatorWorkflowVersion",2);
  }
}

void Window::setSidebarCollapsed(bool collapsed) {
  sidebarCollapsed=collapsed;sidebar->setFixedWidth(collapsed ? 66 : 220);
  findChild<QLabel *>("brandText")->setVisible(!collapsed);
  sidebarToggle->setIcon(QIcon(collapsed ? ":/icons/expand.svg" : ":/icons/collapse.svg"));
  sidebarToggle->setAccessibleName(collapsed ? "Expand navigation" : "Collapse navigation");
  sidebarToggle->setToolTip(collapsed ? "Expand navigation · Ctrl+B" : "Collapse navigation · Ctrl+B");
  for(auto button:nav) {button->setText(collapsed ? "" : button->property("fullText").toString());button->setProperty("collapsed",collapsed);button->style()->unpolish(button);button->style()->polish(button);}
  auto more=findChild<QPushButton *>("moreTools");more->setText(collapsed ? "" : "More tools");
  settings.setValue("sidebarCollapsed",collapsed);
  sidebar->layout()->activate();root->layout()->activate();
}

void Window::automaticCapture() { /* Recording always requires an explicit action. */ }
void Window::configurationChanged() {
  configurationRetry.stop();
  if(!connecting && !backend.acquisition.active() && backend.acquisition.connected()) backend.acquisition.stop();
  refreshOperatorState();
}
void Window::dragEnterEvent(QDragEnterEvent *event) {
  if(!unlocked || !event->mimeData()->hasUrls() || event->mimeData()->urls().size()!=1) return;
  const auto url=event->mimeData()->urls().front();
  const auto suffix=QFileInfo(url.toLocalFile()).suffix().toLower();
  if(url.isLocalFile() && (suffix=="pdf" || suffix=="json")) event->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *event) {
  if(!unlocked || !event->mimeData()->hasUrls() || event->mimeData()->urls().size()!=1) return;
  const auto url=event->mimeData()->urls().front();const auto suffix=QFileInfo(url.toLocalFile()).suffix().toLower();
  if(!url.isLocalFile() || (suffix!="pdf" && suffix!="json")) return;
  event->acceptProposedAction();importIcdPath(url.toLocalFile());
}

void Window::finishConnection() {
  if(closingCapture || !connecting || connectionFuture.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
  const bool ok=connectionFuture.get();connecting=false;
  if(ok && !automaticHeld) {
    if(connectionIsRecording) {
      if(auto error=desktop::describeSession(currentFile,backend.captureConfig);!error.isEmpty())toast(error);
      backend.archive.clear();backend.replaySource=false;backend.replayPlaying=false;backend.selected={};
      model.records.clear();selectedMonitorId=0;paneSelection={};model.refresh();refreshMonitorPanes();captureClock.restart();
      eventText="Recording started";
    } else {eventText="Devices connected — press Start recording";navigate(4);}
  } else {
    backend.acquisition.stop();
    eventText=automaticHeld ? "Connection cancelled" : QString::fromStdString(backend.acquisition.metrics(false).error);
    toast(eventText);
  }
  refreshOperatorState();
}

void Window::refreshOperatorState() {
  findChild<QLabel *>("simulationBadge")->setText(backend.captureConfig.hardware?"DDC · RECEIVE ONLY":"SIMULATOR");
  if(!captureButton) return;
  const auto metrics=backend.acquisition.metrics(false);
  captureButton->setVisible(true);
  captureButton->setEnabled(!savingSession&&!closingCapture);
  captureButton->setText(savingSession ? "Saving…" : connecting ? "Cancel connection" : backend.acquisition.connected() || backend.acquisition.active() ? "Disconnect devices" : !backend.captureConfig.hardware ? "Start simulator" : metrics.failed ? "Retry connection" : "Connect devices");
  stateBadge->setText(connecting ? "Checking devices" : backend.acquisition.active() ? "● Recording" : metrics.failed ? "● SDK error" : backend.acquisition.connected() ? "Connected" : "Disconnected");
  const auto color=metrics.failed ? "#b33a32" : backend.acquisition.active() && backend.captureConfig.hardware ? "#28624d" : "#62685f";
  const auto badgeStyle=QString("color:%1;font-size:14px;padding:6px;").arg(color);
  if(stateBadge->styleSheet()!=badgeStyle)stateBadge->setStyleSheet(badgeStyle);
  for(unsigned i=0;i<2;++i) if(deviceStatuses[i]) {
    if(deviceTitles[i]) deviceTitles[i]->setText(QString("Device %1  /  %2").arg(backend.captureConfig.logicalDevices[i]).arg(i ? "Internal" : "External"));
    const auto &device=metrics.devices[i];QString status,ink="#62685f";
    switch(device.phase) {
      case aero::DevicePhase::Checking: status="Checking configuration…";break;
      case aero::DevicePhase::Ready: status="Connected · ready to record";break;
      case aero::DevicePhase::Monitoring: status="Monitoring";ink="#28624d";break;
      case aero::DevicePhase::Simulating: status="Simulator · hardware not connected";ink="#8a611f";break;
      case aero::DevicePhase::Failed: status="Not ready · check device setup";ink="#b33a32";break;
      case aero::DevicePhase::Disabled: status="Disabled";break;
      default: status="Not connected";break;
    }
    QString buses;
    if(backend.captureConfig.enabled[i*2]) buses="A";
    if(backend.captureConfig.enabled[i*2+1]) buses+=(buses.isEmpty() ? "B" : " + B");
    deviceStatuses[i]->setText(QString("Channel %1  ·  %2  ·  %3").arg(i+1).arg(buses.isEmpty() ? "No buses" : buses,status));
    const auto deviceStyle=QString("color:%1;font-size:13px;").arg(ink);
    if(deviceStatuses[i]->styleSheet()!=deviceStyle)deviceStatuses[i]->setStyleSheet(deviceStyle);
    deviceStatuses[i]->setToolTip(QString::fromStdString(device.message));
  }
  for(unsigned c=0;c<2;++c)if(paneCounts[c]) {
    const auto &device=metrics.devices[c];const auto count=metrics.sourceCounts[c*2]+metrics.sourceCounts[c*2+1];
    const auto state=connecting?"Checking":device.phase==aero::DevicePhase::Failed?"Connection failed":device.phase==aero::DevicePhase::Disabled?"Disabled":backend.acquisition.active()?(backend.captureConfig.hardware?(count?"Receiving":"Monitoring / no traffic"):"Simulation"):backend.acquisition.connected()?"Connected · ready to record":"Stopped";
    paneCounts[c]->setText(QString("%1 · CH%2 / logical %3 · %4\n%5 recorded · %6 shown").arg(c?"Internal":"External").arg(c+1).arg(backend.captureConfig.logicalDevices[c]).arg(state).arg(count).arg(paneModels[c].rowCount()));
    if(rtGrouped)paneCounts[c]->setText(QString("%1 · CH%2 / logical %3 · %4\nA: %5 messages · B: %6 messages · %7 RT groups%8").arg(c?"Internal":"External").arg(c+1).arg(backend.captureConfig.logicalDevices[c]).arg(state).arg(metrics.sourceCounts[c*2]).arg(metrics.sourceCounts[c*2+1]).arg(rtModels[c].rowCount()).arg(rtDisplayPaused?" · DISPLAY PAUSED":""));
    paneCounts[c]->setToolTip(QString::fromStdString(device.message));
  }
  if(recordButton){recordButton->setVisible(!rtGrouped||!backend.acquisition.active());recordButton->setEnabled(!connecting&&!savingSession&&!closingCapture&&(!backend.captureConfig.hardware||backend.acquisition.connected()));recordButton->setText(backend.acquisition.active()?"Stop && save":"Start recording");}
  if(monitorCsv){monitorCsv->setEnabled(!savingSession && !connecting && QFileInfo::exists(currentFile));monitorCsv->setText(savingSession?"Saving…":backend.acquisition.active()?"Stop && save":"Save / retry CSVs");}
  if(selectedSummary) {
    const auto record=selectedMonitorRecord();
    if(record) {const auto p=aero::interpret(*record);selectedSummary->setText(QString("#%1  ·  CH %2 / %3  ·  Payload RT %4 / SA %5\n%6").arg(record->id).arg(record->channel).arg(record->bus ? "B" : "A").arg(p.payloadCommand.rt).arg(p.payloadCommand.sa).arg(words(*record).left(115)));selectedSummary->setToolTip(QString::fromStdString(aero::protocolDiagnostics(*record,p))+"\n"+QString::fromStdString(aero::statusDescription(p)));}
    else selectedSummary->setText("Select a record to inspect its words, engineering values or plot.");
  }
}

bool Window::nativeEvent(const QByteArray &,void *message,qintptr *) {
#ifdef Q_OS_WIN
  const auto event=static_cast<MSG *>(message);
  if(event->message==WM_DEVICECHANGE && (event->wParam==DBT_DEVICEARRIVAL || event->wParam==DBT_DEVNODES_CHANGED))
    QTimer::singleShot(1500,this,[this]{automaticCapture();});
#else
  Q_UNUSED(message);
#endif
  return false;
}

void Window::exportMonitorCsv() {
  for(auto watcher:findChildren<QFutureWatcherBase *>()) if(watcher->isRunning()) {toast("An export is already running.");return;}
  if(analysisData().empty()) {toast("No captured records to export.");return;}
  const auto directory=settings.value("exportFolder",sessionDir+"/Exports").toString();
  if(!QDir().mkpath(directory)) {toast("Cannot create export folder.");return;}
  const auto path=QDir(directory).filePath("Monitor_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::Id128).left(8)+".csv");
  auto watcher=new QFutureWatcher<aero::ExportResult>(this);
  connect(watcher,&QFutureWatcher<aero::ExportResult>::finished,this,[this,watcher,path] {
    const auto result=watcher->result();applicationEvent("monitor_export_finished",path,{{"records",double(result.count)},{"error",QString::fromStdString(result.error)}});toast(result.error.empty() ? "Exported "+QString::number(result.count)+" records: "+path : QString::fromStdString(result.error));watcher->deleteLater();
  });
  const auto bus=model.busFilter,search=model.search;const auto channel=model.channelFilter,rt=model.rtFilter;
  toast("Exporting selected channel and display filters…");
  watcher->setFuture(QtConcurrent::run([records=analysisData(),mappings=backend.mappings(),path,bus,search,channel,rt] {
    aero::Workbench job;job.profile=mappings;
    for(const auto &record:records) {
      const auto command=aero::command(record.commandWord);
      if(channel && record.channel!=channel) continue;
      if(!bus.isEmpty() && bus!=(record.bus ? "B" : "A")) continue;
      if(rt>=0 && command.rt!=rt) continue;
      if(!search.isEmpty() && !words(record).contains(search,Qt::CaseInsensitive) && !hexword(record.commandWord).contains(search,Qt::CaseInsensitive)) continue;
      job.archive.push_back(record);
    }
    auto result=job.exportData(aero::ExportFormat::RawCsv,0,UINT64_MAX);
    if(!result.error.empty()) return result;
    const auto bytes=QByteArray::fromStdString(result.text);
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) {result.error="CSV export failed";return result;}
    QSaveFile checksum(path+".sha256");const auto digest=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
    if(!checksum.open(QIODevice::WriteOnly) || checksum.write(digest)!=digest.size() || !checksum.commit()) result.error="CSV saved; checksum could not be saved";
    QSaveFile profile(path+".profile.json");const auto json=QJsonDocument(desktop::profileToJson(mappings)).toJson();
    if(!profile.open(QIODevice::WriteOnly) || profile.write(json)!=json.size() || !profile.commit()) result.error="CSV saved; profile could not be saved";
    result.text.clear();return result;
  }));
}

bool Window::operatorWorkflowTest(const QString &directory) {
  QDir().mkpath(directory);QStringList results;bool passed=true;
  QMap<QString,QVariant> original;for(const auto &key:settings.allKeys()) original[key]=settings.value(key);
  settings.setValue("monitorLayout","tabs");settings.setValue("monitorRowMode","raw");
  resize(1500,940);setSidebarCollapsed(false);
  auto check=[&](bool ok,const QString &message){passed&=ok;results<<(ok ? "PASS: " : "FAIL: ")+message;};
  unlocked=true;backend.captureConfig.hardware=false;backend.archive.clear();model.records.clear();
  backend.captureConfig.enabled={true,true,true,true};findChild<QLabel *>("simulationBadge")->setText("SIMULATOR");
  for(int i=0;i<80;++i) {auto record=aero::synthetic(i);model.records.push_back(record);backend.archive.push_back(record);}model.refresh();navigate(4);
  QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();const auto expandedTable=monitor->width();
  setSidebarCollapsed(true);QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();
  check(sidebar->width()==66 && nav[4]->text().isEmpty() && !nav[4]->icon().isNull() && !nav[4]->accessibleName().isEmpty() && !findChild<QPushButton *>("moreTools")->accessibleName().isEmpty() && monitor->width()>expandedTable+100,"Collapsed navigation retains accessible icons and enlarges the table");
  setSidebarCollapsed(false);check(!nav[4]->text().isEmpty(),"Navigation expands with labels");
  deviceTabs->setCurrentIndex(1);check(model.rowCount()>0 && std::all_of(model.visible.begin(),model.visible.end(),[&](int i){return model.records[i].channel==1;}),"Device 0 tab isolates Channel 1 / External");
  deviceTabs->setCurrentIndex(2);check(model.rowCount()>0 && std::all_of(model.visible.begin(),model.visible.end(),[&](int i){return model.records[i].channel==2;}),"Device 1 tab isolates Channel 2 / Internal");
  findChild<QPushButton *>("busBButton")->click();check(model.rowCount()>0 && std::all_of(model.visible.begin(),model.visible.end(),[&](int i){return model.records[i].channel==2 && model.records[i].bus==1;}),"Bus filter preserves the selected device");
  findChild<QPushButton *>("allBusButton")->click();deviceTabs->setCurrentIndex(0);
  check(model.rowCount()==80,"Both channels restores all retained records");
  check(monitor->viewport()->height()>300,"Monitor receives the main workspace height");
  settings.setValue("exportFolder",directory);exportMonitorCsv();
  QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<10000) {QApplication::processEvents();bool running=false;for(auto watcher:findChildren<QFutureWatcherBase *>()) running|=watcher->isRunning();if(!running) break;QThread::msleep(5);}
  check(!QDir(directory).entryList({"Monitor_*.csv"},QDir::Files).isEmpty(),"One-click CSV writes a real export");
  monitor->selectRow(6);QApplication::processEvents();grab().save(directory+"/monitor-expanded.png");
  setSidebarCollapsed(true);QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();grab().save(directory+"/monitor-collapsed.png");setSidebarCollapsed(false);
  const auto previousSessionDir=sessionDir;QTemporaryDir captureDirectory;sessionDir=captureDirectory.path();
  for(int ch=1;ch<=2;++ch) for(const auto bus:{"A","B"}) settings.setValue(QString("channel%1Bus %2").arg(ch).arg(bus),true);
  backend.captureConfig.simulatorRate=1000;startCapture();navigate(4);
  QEventLoop first;QTimer::singleShot(250,&first,&QEventLoop::quit);first.exec();monitor->selectRow(0);
  const auto selected=selectedMonitorId;
  QEventLoop later;QTimer::singleShot(300,&later,&QEventLoop::quit);later.exec();
  check(selected && monitor->currentIndex().isValid() && model.at(monitor->currentIndex().row())->id==selected,"Live batches preserve the operator's selected record");
  backend.stopRecording();sessionDir=previousSessionDir;
  automaticHeld=true;configurationRetry.stop();configurationChanged();automaticCapture();
  check(automaticHeld && !configurationRetry.isActive() && !backend.acquisition.active(),"Stop holds capture stopped across configuration and arrival retries");
  automaticHeld=false;
  auto target=aero::synthetic(0);target.id=1001;target.channel=2;target.bus=1;target.timestampNs=1234567890;target.words[0]=2000;
  auto other=target;other.id=1002;other.channel=1;other.bus=0;
  backend.applyProfile(aero::Workbench::demoSignals());backend.archive={other,target};model.records=backend.archive;model.refresh();navigate(4);
  monitor->setColumnHidden(17,false);monitor->setCurrentIndex(model.index(1,17));plotMonitorSelection();
  check(page==7 && analysisChannel==2 && analysisBus==1 && plotRawColumn<0 && !plots.empty() && plots.front()->pointCount()==1,"Word selection plots the mapped engineering field on its own channel/bus clock");
  plots.front()->onPick(QPointF(target.timestampNs/1e9,20));check(backend.selected.id==target.id,"Plot cursor resolves the original record on the selected stream");
  auto evicted=target;evicted.id=1003;evicted.timestampNs+=1000000000;backend.archive={evicted};
  plots.front()->onPick(QPointF(target.timestampNs/1e9,20));check(backend.selected.id==0,"An evicted plotted record cannot resolve to unrelated newer raw data");backend.archive={other,target};
  navigate(4);monitor->setColumnHidden(9,false);monitor->setCurrentIndex(model.index(1,9));plotMonitorSelection();
  check(page==7 && plotRawColumn==9 && plots.front()->pointCount()==1,"An unmapped numeric column plots raw values against card time");
  settings.clear();for(auto i=original.begin();i!=original.end();++i) settings.setValue(i.key(),i.value());settings.sync();
  QFile report(directory+"/operator-workflow.txt");if(!report.open(QIODevice::WriteOnly)) return false;report.write(results.join('\n').toUtf8());return passed;
}
