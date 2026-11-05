#include "main_window.hpp"
#include "services/session_store.hpp"
#include "services/ddc_discovery.hpp"
#include "record_csv.hpp"
bool Window::sessionWorkflowTest(const QString &directory) {
  QDir().mkpath(directory);QStringList report;bool passed=true;
  QMap<QString,QVariant> previous;for(const auto &k:settings.allKeys())previous[k]=settings.value(k);const auto oldDir=sessionDir;
  auto check=[&](bool ok,const QString &message){passed&=ok;report<<(ok?"PASS: ":"FAIL: ")+message;};
  auto settle=[](int milliseconds){QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();};
  auto wait=[&]{QElapsedTimer t;t.start();for(;;){QApplication::processEvents();bool busy=savingSession||replaySeekPending||replayLoadPending;for(auto w:findChildren<QFutureWatcherBase *>())busy|=w->isRunning();if(!busy||t.elapsed()>30000)break;QThread::msleep(5);}QApplication::processEvents();};
  {
    RecordModel protocolModel;aero::Record r;r.id=1;r.source=1;r.validity=15;r.messageType=2;r.commandWord=(5<<11)|(2<<5)|1;r.commandWord2=(3<<11)|(1<<10)|(7<<5)|1;r.statusWord=3<<11;r.statusWord2=5<<11;r.words={1200};protocolModel.records.push_back(r);
    r.id=2;r.validity=5;r.commandWord=(3<<11)|17;r.commandWord2=0;r.messageType=6;r.statusWord=3<<11;r.statusWord2=0;protocolModel.records.push_back(r);
    r.id=3;r.validity=1;r.commandWord=(31<<11)|(2<<5)|1;r.messageType=8;protocolModel.records.push_back(r);
    r.id=4;r.validity=5;r.commandWord=(1<<10)|(2<<5)|1;r.messageType=1;r.statusWord=0;protocolModel.records.push_back(r);
    r.id=5;r.messageType=15;protocolModel.records.push_back(r);
    r.id=6;r.messageType=1;r.words.clear();protocolModel.records.push_back(r);
    r.id=7;r.words={1200};r.statusWord=0x100;protocolModel.records.push_back(r);
    r.id=8;r.commandWord=(3<<11)|(1<<10)|19;r.messageType=15;protocolModel.records.push_back(r);protocolModel.refresh();
    const auto file=directory+"/protocol-"+QUuid::createUuid().toString(QUuid::Id128)+".atd";aero::Recorder recorder;bool same=recorder.open(std::filesystem::path(file.toStdWString()));for(const auto &record:protocolModel.records)same=same&&recorder.append(record);same=same&&recorder.close();
    aero::RecordingIndex index;const auto scan=index.open(std::filesystem::path(file.toStdWString()));same=same&&scan.error.empty()&&!scan.recovered&&scan.count==protocolModel.records.size();
    const auto header=QString::fromStdString(aero::rawCsvHeader()).trimmed().split(',');
    for(int row=0;row<protocolModel.rowCount();++row){const auto original=protocolModel.records[row],replayed=index.at(row);const auto fields=QString::fromStdString(aero::rawCsvRecord(replayed)).trimmed().split(',');same=same&&original==replayed&&fields.size()==header.size();if(fields.size()!=header.size())continue;
      same=same&&fields[header.indexOf("direction")]==protocolModel.data(protocolModel.index(row,6),Qt::DisplayRole).toString()&&fields[header.indexOf("quality")]==protocolModel.data(protocolModel.index(row,10),Qt::DisplayRole).toString()&&fields[header.indexOf("rt")].toUInt()==protocolModel.data(protocolModel.index(row,4),Qt::DisplayRole).toUInt()&&fields[header.indexOf("words")]==protocolModel.data(protocolModel.index(row,8),Qt::DisplayRole).toString();
      const auto interpreted=aero::interpret(original);
      if(interpreted.mode)same=same&&fields[header.indexOf("word_count")].isEmpty()&&(interpreted.supported?protocolModel.data(protocolModel.index(row,7),Qt::DisplayRole).toString().startsWith("Mode "):fields[header.indexOf("mode_code")].isEmpty()&&fields[header.indexOf("expected_payload_words")].isEmpty()&&protocolModel.data(protocolModel.index(row,7),Qt::DisplayRole).toString()=="—");
    }
    check(same,"Hardware protocol fixtures match Live table, CSV and indexed replay: RT-to-RT, mode, broadcast, RT0, unsupported, missing payload, service request");
  }
  RecordModel shared;RecordModel filtered;filtered.sharedSource=&shared;filtered.channelFilter=2;filtered.busFilter="B";filtered.refresh();
  for(int batch=0;batch<12;++batch){std::vector<aero::Record> records;for(int n=0;n<700;++n)records.push_back(aero::synthetic(batch*700+n));shared.append(records);filtered.syncAppend();}
  check(shared.records.size()==5000&&filtered.rowCount()==1250&&std::all_of(filtered.visible.begin(),filtered.visible.end(),[&](int i){return filtered.rows()[i].channel==2&&filtered.rows()[i].bus==1;}),"Incremental shared display retains exactly 5000 records with correct filtered identities");
  const auto output=directory+"/exports-"+QUuid::createUuid().toString(QUuid::Id128);
  sessionDir=directory;settings.setValue("monitorRowMode","raw");settings.setValue("exportFolder",output);settings.setValue("nextRunName","");settings.setValue("exportFolder","");settings.setValue("monitorLayout","windows");settings.setValue("activeMonitorChannel",1);
  for(int c=1;c<=2;++c)for(auto b:{"A","B"})settings.setValue(QString("channel%1Bus %2").arg(c).arg(b),true);
  unlocked=true;automaticHeld=true;backend.captureConfig.hardware=false;backend.captureConfig.simulatorRate=1000;
  backend.profile.clear();backend.captureConfig.hardware=true;discoveryRunning=true;const auto beforeScanCapture=currentFile;startCapture();check(!connecting&&currentFile==beforeScanCapture,"Hardware connection waits for discovery to finish");discoveryRunning=false;backend.captureConfig.hardware=false;
  startCapture();navigate(4);settle(600);
  check(backend.acquisition.active()&&csvFolderEdit->text().isEmpty()&&runNameEdit->text().isEmpty(),"Monitoring starts without CSV folder or run name");
  backend.captureConfig.hardware=true;rescanDdc();for(int i=0;i<150&&discoveryRunning;++i)settle(100);check(!discoveryRunning&&hardwareDiscovery.value("runtime_probe_blocked").toBool(),"Windows inventory refresh does not probe the SDK during capture");backend.captureConfig.hardware=false;
  check(monitorWorkspace&&paneViews[0]&&paneViews[1]&&paneModels[0].rowCount()>0&&paneModels[1].rowCount()>0,"Movable workspace displays both channels");
  check(channelWindows[0]->geometry().left()<channelWindows[1]->geometry().left(),"Initial channel windows are side by side");
  channelWindows[0]->move(15,25);channelWindows[0]->resize(460,280);persistMonitorWorkspace();check(settings.value("monitorWindow0").toRect().topLeft()==QPoint(15,25),"Child window geometry persists");
  const auto other=paneModels[1].rowCount();auto bus=content->findChild<QComboBox *>("pane0BusFilter");bus->setCurrentIndex(2);
  check(paneModels[1].rowCount()==other&&std::all_of(paneModels[0].visible.begin(),paneModels[0].visible.end(),[&](int i){return paneModels[0].rows()[i].channel==1&&paneModels[0].rows()[i].bus==1;}),"Pane filters are independent");
  paneViews[1]->selectRow(0);const auto selected=selectedMonitorRecord();const auto selectedId=selected?selected->id:0;
  check(selected&&selected->channel==2,"Selection resolves the Internal record");settle(150);
  check(selectedMonitorRecord()&&selectedMonitorRecord()->id==selectedId,"Incoming batches preserve active pane selection");
  inspectSelection();check(page==5&&backend.selected.id==selectedId,"Inspect opens the selected pane record");navigate(4);
  check(!content->findChild<QPushButton *>("pauseButton")->isVisible(),"Live display pause is removed");
  const auto recordedBefore=backend.acquisition.metrics().recorded;auto mode=content->findChild<QComboBox *>("workspaceLayout");mode->setCurrentIndex(1);settle(150);
  check(!monitorWorkspace->isVisible()&&deviceTabs->isVisible()&&backend.acquisition.metrics().recorded>recordedBefore,"Layout switches during uninterrupted capture");mode->setCurrentIndex(0);
  resize(1500,940);settle(50);navigate(3);check(rtLights[0][3]&&rtLights[0][3]->property("responseObserved").toBool(),"Dashboard shows recording-side RT responses");grab().save(directory+"/dashboard.png");navigate(4);
  resize(1500,940);settle(50);grab().save(directory+"/movable.png");resize(900,540);settle(50);grab().save(directory+"/movable-minimum.png");
  check(paneViews[0]->height()>=90&&paneViews[1]->height()>=90&&monitorCsv->isVisible(),"Minimum window keeps both panes and save action available through scrolling");
  QTimer cancelDialog;cancelDialog.setInterval(25);bool sawShortcuts=false;
  connect(&cancelDialog,&QTimer::timeout,this,[&]{for(auto dialog:findChildren<QDialog *>())if(dialog->windowTitle()=="Save Internal and External CSVs"){QStringList text;for(auto button:dialog->findChildren<QPushButton *>())text<<button->text();sawShortcuts=text.contains("Desktop")&&text.contains("Documents")&&text.contains("Browse…");dialog->reject();cancelDialog.stop();}});cancelDialog.start();
  stopAndSave();wait();cancelDialog.stop();check(sawShortcuts,"Missing-destination dialog offers Desktop, Documents and Browse shortcuts");check(!backend.acquisition.active()&&QFileInfo::exists(currentFile)&&sessionSaveState.contains("cancelled"),"Cancelling missing-destination dialog retains finalized recording");
  QHash<quint64,QStringList> liveSnapshot;
  for(const auto &r:model.records)liveSnapshot.insert(r.id,{stamp(r.timestampNs),QString::number(r.channel),r.bus?"B":"A",hexword(r.commandWord),hexword(r.statusWord),words(r),QString::number(aero::command(r.commandWord).rt),QString::number(aero::command(r.commandWord).sa),aero::command(r.commandWord).transmit?"RT→BC":"BC→RT"});
  csvFolderEdit->setText(output);runNameEdit->setText("run1");
  stopAndSave();stopAndSave();check(savingSession,"Duplicate Stop & save is guarded");wait();
  const auto metrics=backend.acquisition.metrics();check(!savingSession&&!backend.acquisition.active()&&sessionSaveState=="Complete","Stop & save finishes asynchronously");
  QFile manifest(currentFile+".export.json");QJsonObject metadata;if(manifest.open(QIODevice::ReadOnly))metadata=QJsonDocument::fromJson(manifest.readAll()).object();manifest.close();
  check(metadata.value("integrity").toString()=="validated"&&metadata.value("capture_error").toString().isEmpty(),"Healthy Windows CRLF status is classified as complete");
  check(metadata.value("internal_records").toString().toULongLong()+metadata.value("external_records").toString().toULongLong()==metrics.recorded&&metrics.recorded>0,"Both CSV totals equal finalized recording, regardless of display filters");
  int matched=0;bool same=true;
  const auto saved=aero::read(std::filesystem::path(currentFile.toStdWString()));
  for(unsigned ch=1;ch<=2;++ch){QFile csv(output+"/run1"+(ch==1?"_EXT.csv":"_INT.csv"));same&=csv.open(QIODevice::ReadOnly);same&=QString::fromUtf8(csv.readLine()).trimmed()==QString::fromStdString(aero::sessionCsvHeader()).trimmed();std::optional<std::uint64_t> previous;
    for(const auto &r:saved.records)if(r.channel==ch){same&=QString::fromUtf8(csv.readLine()).trimmed()==QString::fromStdString(aero::sessionCsvRecord(r,previous)).trimmed();previous=r.timestampNs;++matched;}same&=csv.atEnd();}
  check(same&&matched==int(metrics.recorded)&&matched>0,"Every finalized record matches the 42-column channel CSV with independent time deltas");
  resize(1500,940);settle(50);settings.setValue("monitorLayout","tabs");navigate(4);check(!monitorWorkspace->isVisible()&&deviceTabs->isVisible(),"Settings selects the tab layout");
  grab().save(directory+"/tabs.png");
  check(QDir(output).entryList(QDir::Files|QDir::Hidden).size()==2,"CSV destination contains only the paired CSVs");
  loadSession(currentFile);wait();check(page==11&&backend.replayCount()==metrics.recorded,"Saved session opens for indexed replay");
  if(backend.replayCount()){seekReplay(backend.replayCount()-1);wait();check(backend.selected.id>0&&backend.replayIndex==backend.replayCount()-1,"Replay seeks to the end");}
  const auto liveCapture=currentFile;const auto older=directory+"/different-replay-"+QUuid::createUuid().toString(QUuid::Id128)+".atd";aero::Recorder otherWriter;bool otherCreated=otherWriter.open(std::filesystem::path(older.toStdWString()));auto otherRecord=aero::synthetic(99);otherRecord.words[0]=0xBEEF;otherCreated=otherCreated&&otherWriter.append(otherRecord)&&otherWriter.close();check(otherCreated,"Different replay fixture has distinct words and record identity");
  QFile otherStatus(older+".status.txt");if(otherStatus.open(QIODevice::WriteOnly)){otherStatus.write("integrity=CLOSED\r\nerror=\r\n");otherStatus.close();}desktop::describeSession(older,backend.captureConfig);
  loadSession(older);wait();check(backend.replayRecords.size()==1&&backend.replayRecords[0].words[0]==0xBEEF,"Older replay differs from last live capture");
  loadSession(liveCapture);loadSession(older);wait();check(currentReplay==older&&backend.replayRecords.size()==1&&backend.replayRecords[0].words[0]==0xBEEF,"Latest replay request wins overlapping index loads");navigate(4);stopAndSave();wait();
  check(sessionSaveState=="Complete"&&!QFileInfo::exists(older+".export.json")&&currentFile==liveCapture,"Returning from another replay keeps Live monitor CSV export bound to the live recording");
  settings.setValue("exportFolder",output+"/selected-session");settings.setValue("nextRunName","chosen");stopAndSave(older);wait();check(sessionSaveState=="Complete"&&currentFile==liveCapture&&QFileInfo::exists(older+".export.json"),"Exporting a selected session does not replace the live capture identity");
  const auto emptyPath=directory+"/empty-"+QUuid::createUuid().toString(QUuid::Id128)+".atd";aero::Recorder emptyWriter;check(emptyWriter.open(std::filesystem::path(emptyPath.toStdWString()))&&emptyWriter.close(),"Empty replay fixture is a valid finalized recording");loadSession(emptyPath);wait();check(backend.replaySource&&backend.replayCount()==0&&analysisData().empty(),"An empty replay never falls back to unrelated live data");navigate(4);check(!backend.replaySource&&currentFile==liveCapture,"Live view restores its own source after an empty replay");
  navigate(1);settle(50);grab().save(directory+"/welcome.png");navigate(2);settle(50);grab().save(directory+"/device.png");
  rescanDdc();wait();check(hardwareDiscovery.contains("driver_store_evidence")&&hardwareDiscovery.contains("logical_assignments"),"Discovery records installed drivers and vendor logical assignments");
  QFile evidence(directory+"/ddc-discovery.json");if(evidence.open(QIODevice::WriteOnly))evidence.write(QJsonDocument(hardwareDiscovery).toJson());
  backend.recordingIndex.reset();backend.replaySource=false;sessionDir=oldDir;settings.clear();for(auto i=previous.begin();i!=previous.end();++i)settings.setValue(i.key(),i.value());
  QFile file(directory+"/session-workflow.txt");if(file.open(QIODevice::WriteOnly))file.write(report.join('\n').toUtf8());else passed=false;return passed;
}


bool Window::hardwareConnectionTest(const QString &reportDirectory,const QString &runtime) {
  const auto directory=reportDirectory+"/"+QUuid::createUuid().toString(QUuid::Id128);
  QDir().mkpath(directory);QStringList checks;bool passed=true;
  QMap<QString,QVariant> previous;for(auto k:settings.allKeys())previous[k]=settings.value(k);
  auto check=[&](bool ok,const QString &name){passed&=ok;checks<<(ok?"PASS: ":"FAIL: ")+name;};
  auto wait=[&](auto ready){QElapsedTimer clock;clock.start();while(!ready()&&clock.elapsed()<15000){QApplication::processEvents();QThread::msleep(10);}QApplication::processEvents();return ready();};
  wait([&]{return !discoveryRunning;});backend.acquisition.stop();unlocked=true;sessionDir=directory;
  backend.captureConfig.hardware=true;backend.captureConfig.runtime=std::filesystem::path(runtime.toStdWString());backend.captureConfig.logicalDevices={0,1};
  settings.setValue("logicalChannel1",0);settings.setValue("logicalChannel2",1);
  settings.setValue("ddcAssignmentMode","card_manager");
  const QString board="USB\\VID_4DDC&PID_0300\\SDK-FIXTURE";
  hardwareDiscovery={{"sdk_fixture",true},{"device_enumeration_complete",true},{"physical_presence","detected"},
    {"devices",QJsonArray{QJsonObject{{"name","SDK fixture — no physical card"},{"instance_id",board}}}},
    {"logical_assignments",QJsonArray{desktop::classifyDdcAssignment(0,{{"DeviceID",board},{"Channel",0},{"Family",3}}),desktop::classifyDdcAssignment(1,{{"DeviceID",board},{"Channel",1},{"Family",3}})}}};
  for(int c=1;c<=2;++c)for(auto b:{"A","B"})settings.setValue(QString("channel%1Bus %2").arg(c).arg(b),true);
  settings.setValue("exportFolder",directory+"/csv");settings.setValue("nextRunName","run1");
  navigate(4);wait([&]{return !discoveryRunning;});const auto before=QDir(directory).entryList({"*.atd"},QDir::Files).size();
  connectDevices();wait([&]{return !connecting&&!connectAfterDiscovery&&!discoveryRunning;});
  check(backend.acquisition.connected()&&!backend.acquisition.active()&&backend.acquisition.metrics().recorded==0,"Connect initializes without starting acquisition");
  check(QDir(directory).entryList({"*.atd"},QDir::Files).size()==before,"Connect creates no recording file");
  check(recordButton&&recordButton->isEnabled()&&recordButton->text()=="Start recording","Live Monitor exposes Start recording after Connect");
  for(int cycle=0;cycle<3;++cycle){
    if(recordButton)recordButton->click();wait([&]{return !connecting;});wait([&]{return backend.acquisition.metrics().recorded>=4||backend.acquisition.error();});
    check(backend.acquisition.active()&&backend.acquisition.metrics().recorded>=4,QString("Cycle %1 receives data after Start recording").arg(cycle+1));
    if(cycle==2)disconnectDevices();else stopAndSave();wait([&]{return !savingSession;});
    check(!backend.acquisition.active()&&sessionSaveState=="Complete",QString("Cycle %1 stops and saves").arg(cycle+1));
    check(backend.acquisition.connected()==(cycle<2),"Stop retains connection; Disconnect releases it after save");
  }
  connectDevices();wait([&]{return !connecting&&!connectAfterDiscovery&&!discoveryRunning;});check(backend.acquisition.connected()&&!backend.acquisition.active(),"Reconnect does not auto-record");disconnectDevices();
  check(QDir(directory+"/csv").entryList({"*_EXT.csv","*_INT.csv"},QDir::Files).size()==6,"Three runs retain distinct paired CSVs");
  backend.captureConfig.runtime=std::filesystem::path((directory+"/missing-runtime.dll").toStdWString());connectDevices();wait([&]{return !connecting&&!connectAfterDiscovery&&!discoveryRunning;});
  check(!backend.acquisition.connected()&&recordButton&&!recordButton->isEnabled(),"Failed SDK selection keeps Start recording disabled");
  const auto failure=connectionAttempt.value("error").toString();check(!failure.isEmpty()&&connectionAttempt.value("failure").toObject().contains("stage"),"Global pre-channel failure preserves structured connection detail");
  QEventLoop expiredToast;QTimer::singleShot(8200,&expiredToast,&QEventLoop::quit);expiredToast.exec();navigate(2);navigate(4);
  const auto persistent=content->findChild<QLabel *>("connectionResultSummary"),reason=content->findChild<QLabel *>("recordingDisabledReason");
  check(persistent&&persistent->text().contains(failure)&&reason&&reason->text().contains(failure),"Failure and disabled reason survive toast expiry and navigation");
  check(connectionAttempt.value("resolved_identity").isObject(),"Connection report retains resolved backend identity independently of discovery");
  navigate(2);QApplication::processEvents();
  auto method=content->findChild<QComboBox *>("ddcBackend");auto receive=content->findChild<QComboBox *>("ddcReceiveMode");
  check(method&&receive,"Designer connection method and receive controls are present");
  if(method&&receive){method->setCurrentIndex(1);receive->setCurrentIndex(2);QApplication::processEvents();
    check(backend.captureConfig.backend==aero::DdcBackend::HelperX86&&backend.captureConfig.receiveMode==aero::DdcReceiveMode::LegacyMT&&!backend.acquisition.connected()&&!connecting,"Manual method and mode selection does not connect");
    check(settings.value("ddcBackend").toString()=="helper"&&settings.value("ddcReceiveMode").toInt()==2,"Selected method and mode persist in existing settings");
    method->setCurrentIndex(0);receive->setCurrentIndex(0);
  }
  backend.captureConfig.runtime=std::filesystem::path(runtime.toStdWString());
  QFile report(reportDirectory+"/hardware-connection.txt");if(report.open(QIODevice::WriteOnly))report.write(checks.join('\n').toUtf8());else passed=false;
  settings.clear();for(auto it=previous.begin();it!=previous.end();++it)settings.setValue(it.key(),it.value());settings.sync();
  return passed;
}
