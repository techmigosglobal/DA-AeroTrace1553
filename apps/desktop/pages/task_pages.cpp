#include "main_window.hpp"
#include "services/task_services.hpp"
#include "services/profile_io.hpp"
#include "ui_task_dialog.h"
#include "services/ui_observer.hpp"

QString Window::applyReviewedAction(const QJsonObject &proposal,const QJsonObject &base,const QString &hash) {
  if(desktop::profileToJson(backend.mappings())!=base)return "Active mappings changed; generate a new preview";
  const auto id=proposal["signal"].toString();const auto mappings=backend.mappings();const auto found=std::find_if(mappings.begin(),mappings.end(),[&](const auto &s){return QString::fromStdString(s.id)==id;});
  if(found==mappings.end() || hash.isEmpty() || QString::fromStdString(found->sourceHash)!=hash)return "The proposal does not match the active ICD";
  const auto kind=proposal["kind"].toString();std::vector<aero::Signal> updated;
  if(kind=="mapping") {
    if(connecting || backend.acquisition.active())return "Stop capture before changing conversion rules";
    if(auto error=desktop::profileFromJson(proposal["profile"].toObject(),updated);!error.isEmpty())return error;
    if(updated.size()!=mappings.size())return "A clarification must preserve the selected profile";
    const QStringList allowed={"scale","offset","word","lsb","width","rt","sa","channel","bus","signed","transmit","numeric_type","lsw_first","unit","operator_note"};int changedFields=0;
    for(std::size_t i=0;i<updated.size();++i) {
      const auto before=desktop::signalToJson(mappings[i]),after=desktop::signalToJson(updated[i]);
      if(mappings[i].id!=id.toStdString()){if(before!=after)return "Proposal changed an unrelated signal";continue;}
      for(const auto &key:after.keys())if(before[key]!=after[key]){if(!allowed.contains(key))return "Proposal changed signal identity or provenance";if(key!="operator_note")++changedFields;}
    }
    if(changedFields>1)return "Preview one conversion field at a time";
    if(auto error=QString::fromStdString(aero::validateChecks(backend.captureConfig.checkRules,updated));!error.isEmpty())return error;
  } else if(kind!="plot") return "Unsupported reviewed action";
  const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ReviewedActions";if(!QDir().mkpath(directory))return "Cannot create action audit folder";
  const QJsonObject audit{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"document_hash",hash},{"before",base},{"proposal",proposal}};
  if(auto error=desktop::writeJson(directory+"/"+QUuid::createUuid().toString(QUuid::Id128)+".json",audit);!error.isEmpty())return error;
  if(kind=="mapping") {if(auto error=backend.applyProfile(std::move(updated));!error.empty())return QString::fromStdString(error);persistProfile();}
  preferredSignalIndex=unsigned(found-mappings.begin());plotRawColumn=-1;
  analysisChannel=found->channel?found->channel:1;analysisBus=found->bus>=0?found->bus:0;
  if(kind=="plot")navigate(7);else refreshDataViews();
  applicationEvent("reviewed_action_applied",kind+" "+id);return {};
}

QJsonObject Window::taskSnapshot(const QString &name) const {
  auto c=backend.captureConfig;for(int k=0;k<4;++k)c.enabled[k]=settings.value(QString("channel%1Bus %2").arg(k/2+1).arg(k%2?'B':'A'),true).toBool();QJsonArray streams;for(bool enabled:c.enabled)streams.append(enabled);
  QJsonObject display;for(int i=0;i<49;++i)display[QString("monitorColumn%1").arg(i)]=settings.value(QString("monitorColumn%1").arg(i),i!=7 && i!=9 && i<11).toBool();
  const auto mappings=backend.mappings();QJsonArray extra;for(const auto &id:extraPlotIds)if(std::any_of(mappings.begin(),mappings.end(),[&](const auto &s){return QString::fromStdString(s.id)==id;}))extra.append(id);
  const auto selected=preferredSignalIndex<mappings.size()?preferredSignalIndex:0;
  return {{"schema","aerotrace.task.v1"},{"name",name},{"profile",desktop::profileToJson(backend.mappings())},
    {"capture",QJsonObject{{"hardware",c.hardware},{"streams",streams},{"devices",QJsonArray{c.logicalDevices[0],c.logicalDevices[1]}},{"runtime",QString::fromStdWString(c.runtime.wstring())},{"mapped_simulation",!c.simulatorProfile.empty()},{"simulation",desktop::simulationToJson(c)},{"checks",desktop::checksToJson(c.checkRules)}}},
    {"view",QJsonObject{{"page",page},{"channel",model.channelFilter},{"bus",model.busFilter},{"rt",model.rtFilter},{"search",model.search},{"signal",int(selected)},{"extra_signals",extra},{"plot_channel",int(analysisChannel)},{"plot_bus",analysisBus},{"raw_column",plotRawColumn},{"dense",settings.value("dense",false).toBool()},{"columns",display},{"sidebar_collapsed",sidebarCollapsed}}}};
}
QString Window::restoreTask(const QJsonObject &o) {
  if(connecting || backend.acquisition.active())return "Stop capture before opening a workspace";
  if(o["schema"]!="aerotrace.task.v1" || !o["capture"].isObject() || !o["view"].isObject())return "Unsupported workspace";
  std::vector<aero::Signal> profile;const auto savedProfile=o["profile"].toObject();
  if(!(savedProfile["schema"]=="aerotrace.icd.profile.v2" && savedProfile["signals"].isArray() && savedProfile["signals"].toArray().empty()))if(auto error=desktop::profileFromJson(savedProfile,profile);!error.isEmpty())return error;
  auto c=backend.captureConfig;const auto capture=o["capture"].toObject();const auto streams=capture["streams"].toArray(),devices=capture["devices"].toArray();
  if(streams.size()!=4 || devices.size()!=2 || !capture["hardware"].isBool() || !capture["mapped_simulation"].isBool())return "Invalid capture configuration";
  bool enabled=false;for(int k=0;k<4;++k){if(!streams[k].isBool())return "Invalid stream";c.enabled[k]=streams[k].toBool();enabled|=c.enabled[k];}if(!enabled)return "Enable at least one stream";
  for(int k=0;k<2;++k){if(!devices[k].isDouble() || devices[k].toDouble()<0 || devices[k].toDouble()>255 || devices[k].toDouble()!=devices[k].toInt())return "Invalid device number";c.logicalDevices[k]=devices[k].toInt();}
  c.hardware=capture["hardware"].toBool();c.runtime=std::filesystem::path(capture["runtime"].toString().toStdWString());c.simulatorProfile=capture["mapped_simulation"].toBool()?profile:std::vector<aero::Signal>{};
  if(capture["mapped_simulation"].toBool() && profile.empty())return "ICD generation requires a conversion profile";
  if(auto error=desktop::simulationFromJson(capture["simulation"].toObject(),c);!error.isEmpty())return error;
  if(!capture["checks"].isArray())return "Missing checks";
  if(auto error=desktop::checksFromJson(capture["checks"].toArray(),profile,c.checkRules);!error.isEmpty())return error;
  const auto view=o["view"].toObject();
  QStringList extra;const auto extraValues=view["extra_signals"].toArray();if(extraValues.size()>4)return "Maximum four added plot traces";
  for(const auto &v:extraValues){if(!v.isString() || std::none_of(profile.begin(),profile.end(),[&](const auto &s){return QString::fromStdString(s.id)==v.toString();}))return "Unknown added plot trace";extra.append(v.toString());}
  auto number=[&](const char *key,int lo,int hi){return view[key].isDouble() && view[key].toDouble()==view[key].toInt() && view[key].toInt()>=lo && view[key].toInt()<=hi;};
  if(!number("page",0,17)||!number("channel",0,2)||!number("rt",-1,31)||!number("signal",0,std::max(0,int(profile.size())-1))||!number("plot_channel",1,2)||!number("plot_bus",0,1)||!number("raw_column",-1,48)||!QStringList{"","A","B"}.contains(view["bus"].toString())||view["search"].toString().size()>2000)return "Invalid workspace view";
  if(auto error=backend.applyProfile(profile);!error.empty())return QString::fromStdString(error);
  automaticHeld=true;configurationRetry.stop();backend.captureConfig=c;backend.replayPlaying=false;
  extraPlotIds=extra;
  model.channelFilter=view["channel"].toInt();model.busFilter=view["bus"].toString();model.rtFilter=view["rt"].toInt();model.search=view["search"].toString();preferredSignalIndex=view["signal"].toInt();analysisChannel=view["plot_channel"].toInt();analysisBus=view["plot_bus"].toInt();plotRawColumn=view["raw_column"].toInt();
  settings.setValue("captureSource",c.hardware?"DDC":"SIMULATOR");settings.setValue("simulatorRate",c.simulatorRate);
  for(int k=0;k<4;++k)settings.setValue(QString("channel%1Bus %2").arg(k/2+1).arg(k%2?'B':'A'),c.enabled[k]);
  for(int k=0;k<2;++k)settings.setValue(QString("logicalChannel%1").arg(k+1),c.logicalDevices[k]);
  settings.setValue("ddcRuntime",QString::fromStdWString(c.runtime.wstring()));settings.setValue("dense",view["dense"].toBool());
  const auto columns=view["columns"].toObject();for(int i=0;i<49;++i){const auto key=QString("monitorColumn%1").arg(i);if(columns[key].isBool())settings.setValue(key,columns[key].toBool());}
  setSidebarCollapsed(view["sidebar_collapsed"].toBool());persistProfile();model.refresh();navigate(view["page"].toInt()==0?4:view["page"].toInt());refreshOperatorState();return {};
}
void Window::taskCenter() {
  if(taskDialog){taskDialog->show();taskDialog->raise();return;}
  auto dialog=new QDialog(this);taskDialog=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);Ui::TaskDialog form;form.setupUi(dialog);
  form.cancelBatch->setEnabled(false);form.openReport->setEnabled(false);form.batchOutput->setText(settings.value("exportDirectory",sessionDir).toString());
  form.checks->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  auto add=[this,form](const QJsonObject &o) {
    const auto row=form.checks->rowCount();form.checks->insertRow(row);auto signal=new QComboBox(form.checks);for(const auto &s:backend.mappings())signal->addItem(QString::fromStdString(s.id));const auto id=o["signal"].toString();if(!id.isEmpty() && signal->findText(id)<0){signal->addItem(id);signal->setToolTip("This saved signal is absent from the current profile; choose a valid signal or remove this check");}if(!id.isEmpty())signal->setCurrentText(id);form.checks->setCellWidget(row,0,signal);
    const QStringList keys={"minimum","maximum","missing_ms","hysteresis"};for(int i=0;i<4;++i)form.checks->setItem(row,i+1,new QTableWidgetItem(o.contains(keys[i])?QString::number(o[keys[i]].toDouble(),'g',15):QString()));
  };
  auto fill=[this,form,add] {
    const auto &c=backend.captureConfig;form.useSimulation->setChecked(!c.hardware);form.useProfile->setChecked(!c.simulatorProfile.empty());form.wave->setCurrentText(QString::fromStdString(c.simulatorWave));form.rate->setValue(c.simulatorRate);form.amplitude->setValue(c.simulatorAmplitude);form.offset->setValue(c.simulatorOffset);form.frequency->setValue(c.simulatorFrequency);form.seed->setText(QString::number(c.simulatorSeed));form.dropEvery->setValue(c.simulatorDropEvery);form.flagEvery->setValue(c.simulatorFlagEvery);
    form.checks->setRowCount(0);for(const auto &v:desktop::checksToJson(c.checkRules))add(v.toObject());
  };fill();
  connect(form.clearTraces,&QPushButton::clicked,dialog,[this,form]{extraPlotIds.clear();if(page==7)navigate(7);form.status->setText("Added plot traces cleared");});
  connect(form.addCheck,&QPushButton::clicked,dialog,[form,add]{if(form.checks->rowCount()<512)add(QJsonObject{{"missing_ms",0},{"hysteresis",0}});});
  connect(form.removeCheck,&QPushButton::clicked,dialog,[form]{if(form.checks->currentRow()>=0)form.checks->removeRow(form.checks->currentRow());});
  connect(form.apply,&QPushButton::clicked,dialog,[this,form] {
    if(connecting || backend.acquisition.active()){form.status->setText("Stop capture before applying configuration");return;}
    if(form.useProfile->isChecked() && backend.profile.empty()){form.status->setText("Activate an ICD profile before enabling ICD-based generation");return;}
    auto c=backend.captureConfig;for(int k=0;k<4;++k)c.enabled[k]=settings.value(QString("channel%1Bus %2").arg(k/2+1).arg(k%2?'B':'A'),true).toBool();c.hardware=!form.useSimulation->isChecked();c.simulatorProfile=form.useProfile->isChecked()?backend.mappings():std::vector<aero::Signal>{};
    const QJsonObject simulation{{"rate",form.rate->value()},{"wave",form.wave->currentText()},{"amplitude",form.amplitude->value()},{"offset",form.offset->value()},{"frequency",form.frequency->value()},{"seed",form.seed->text()},{"drop_every",form.dropEvery->value()},{"flag_every",form.flagEvery->value()}};
    if(auto error=desktop::simulationFromJson(simulation,c);!error.isEmpty()){form.status->setText(error);return;}
    QJsonArray values;const QStringList keys={"minimum","maximum","missing_ms","hysteresis"};
    for(int row=0;row<form.checks->rowCount();++row){QJsonObject o{{"signal",qobject_cast<QComboBox *>(form.checks->cellWidget(row,0))->currentText()}};for(int col=1;col<5;++col){const auto text=form.checks->item(row,col)->text().trimmed();if(text.isEmpty())continue;bool ok=false;const auto value=QLocale::c().toDouble(text,&ok);if(!ok || !std::isfinite(value)){form.status->setText("Check values must be finite numbers (decimal point)");return;}o[keys[col-1]]=value;}values.append(o);}
    if(auto error=desktop::checksFromJson(values,backend.mappings(),c.checkRules);!error.isEmpty()){form.status->setText(error);return;}
    automaticHeld=true;configurationRetry.stop();backend.captureConfig=c;settings.setValue("captureSource",c.hardware?"DDC":"SIMULATOR");settings.setValue("simulatorRate",c.simulatorRate);refreshOperatorState();form.status->setText("Applied. Start capture explicitly; save a workspace to reuse this setup.");
  });
  connect(form.saveTask,&QPushButton::clicked,dialog,[this,form]{const auto path=QFileDialog::getSaveFileName(this,"Save task workspace",sessionDir+"/"+(form.taskName->text().isEmpty()?"Workspace":form.taskName->text())+".aerotask.json","AeroTrace workspace (*.aerotask.json)");if(path.isEmpty())return;const auto error=desktop::writeJson(path,taskSnapshot(form.taskName->text()));form.status->setText(error.isEmpty()?"Workspace saved using the applied configuration":error);});
  connect(form.openTask,&QPushButton::clicked,dialog,[this,form,fill]{const auto path=QFileDialog::getOpenFileName(this,"Open task workspace",sessionDir,"AeroTrace workspace (*.aerotask.json)");if(path.isEmpty())return;QJsonObject o;auto error=desktop::readJson(path,o);if(error.isEmpty())error=restoreTask(o);if(error.isEmpty()){fill();form.taskName->setText(o["name"].toString());}form.status->setText(error.isEmpty()?"Workspace restored; capture is stopped":error);});
  connect(form.chooseFiles,&QPushButton::clicked,dialog,[this,form]{const auto paths=QFileDialog::getOpenFileNames(this,"Choose recordings",sessionDir,"ATD2 recordings (*.atd *.atd2)");if(paths.isEmpty())return;form.batchFiles->clear();form.batchFiles->addItems(paths);});
  connect(form.outputFolder,&QPushButton::clicked,dialog,[this,form]{const auto path=QFileDialog::getExistingDirectory(this,"Batch output",form.batchOutput->text());if(!path.isEmpty())form.batchOutput->setText(path);});
  struct Job {std::atomic<bool> cancelled{};std::atomic<int> progress{};bool running{};QString output;};auto job=std::make_shared<Job>();
  auto timer=new QTimer(dialog);timer->setInterval(150);connect(timer,&QTimer::timeout,dialog,[form,job]{form.batchProgress->setValue(job->progress.load());});
  connect(form.runBatch,&QPushButton::clicked,dialog,[this,dialog,form,job,timer] {
    if(job->running)return;if(connecting || backend.acquisition.active()){form.status->setText("Stop capture before batch analysis");return;}QStringList paths;for(int i=0;i<form.batchFiles->count();++i)paths<<form.batchFiles->item(i)->text();
    if(paths.isEmpty() || paths.size()>100 || form.batchOutput->text().isEmpty()){form.status->setText("Choose 1–100 inputs and an output folder");return;}
    job->running=true;job->cancelled=false;job->progress=0;form.batchProgress->setRange(0,paths.size());form.runBatch->setEnabled(false);form.cancelBatch->setEnabled(true);form.openReport->setEnabled(false);form.status->setText("Analyzing recordings…");timer->start();
    auto watcher=new QFutureWatcher<QJsonObject>(dialog);const auto profile=backend.mappings();const auto rules=backend.captureConfig.checkRules;const auto output=form.batchOutput->text();
    connect(watcher,&QFutureWatcher<QJsonObject>::finished,dialog,[form,job,timer,watcher]{timer->stop();job->running=false;const auto result=watcher->result();job->output=result["output"].toString();form.batchProgress->setValue(job->progress.load());form.runBatch->setEnabled(true);form.cancelBatch->setEnabled(false);form.openReport->setEnabled(!job->output.isEmpty());form.status->setText(result.contains("error")?result["error"].toString():QString("%1: %2 processed, %3 exported, %4 failed. Recovered inputs are marked incomplete in the report.").arg(result["cancelled"].toBool()?"Cancelled":"Finished").arg(result["processed"].toInt()).arg(result["completed_files"].toInt()).arg(result["failed_files"].toInt()));watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([paths,output,profile,rules,job]{try{return desktop::batchAnalyze(paths,output,profile,rules,&job->cancelled,&job->progress);}catch(const std::exception &e){return QJsonObject{{"error",QString::fromUtf8(e.what())}};}}));
  });
  connect(form.cancelBatch,&QPushButton::clicked,dialog,[form,job]{job->cancelled=true;form.status->setText("Cancelling after the current bounded operation…");});
  connect(form.openReport,&QPushButton::clicked,dialog,[job]{QDesktopServices::openUrl(QUrl::fromLocalFile(job->output));});
  connect(form.close,&QPushButton::clicked,dialog,&QDialog::close);connect(dialog,&QObject::destroyed,this,[job]{job->cancelled=true;});
  observeUiActions(dialog);dialog->show();
}

bool Window::automationWorkflowTest(const QString &directory) {
  auto check=[](bool result,const char *message){if(!result)throw std::runtime_error(message);};
  try {
    QDir().mkpath(directory);unlocked=true;automaticHeld=true;backend.stopRecording();
    aero::Signal signal{"TEMP","degC"};signal.scale=.1;signal.sourceHash=std::string(64,'a');signal.sourceDocument="fixture.pdf";signal.sourcePage=1;
    check(backend.applyProfile({signal}).empty(),"profile");backend.captureConfig.hardware=false;backend.captureConfig.simulatorProfile={signal};backend.captureConfig.simulatorWave="constant";backend.captureConfig.simulatorOffset=20;
    backend.captureConfig.checkRules={{"TEMP",0.,30.,100000000,1}};model.rtFilter=3;model.search="0BB8";preferredSignalIndex=0;analysisChannel=2;analysisBus=1;plotRawColumn=-1;extraPlotIds={"TEMP"};navigate(4);
    const auto saved=taskSnapshot("Acceptance");check(desktop::writeJson(directory+"/fixture.aerotask.json",saved).isEmpty(),"save workspace");
    model.rtFilter=-1;model.search.clear();backend.captureConfig.checkRules.clear();extraPlotIds.clear();check(restoreTask(saved).isEmpty(),"restore workspace");check(model.rtFilter==3 && model.search=="0BB8" && analysisChannel==2 && analysisBus==1 && extraPlotIds==QStringList{"TEMP"} && backend.captureConfig.checkRules.size()==1 && automaticHeld && !backend.acquisition.active(),"restored values");
    auto invalid=saved;auto invalidView=invalid["view"].toObject();invalidView["signal"]=999;invalid["view"]=invalidView;check(!restoreTask(invalid).isEmpty() && model.rtFilter==3,"reject invalid workspace atomically");
    const auto base=desktop::profileToJson(backend.mappings());auto changed=signal;changed.scale=.2;changed.operatorNote="Set TEMP scale to 0.2";
    const QJsonObject proposal{{"kind","mapping"},{"signal","TEMP"},{"profile",desktop::profileToJson({changed})},{"summary","Change scale"}};
    check(backend.profile[0].scale==.1,"preview must not mutate");check(applyReviewedAction(proposal,base,QString(64,'a')).isEmpty(),"apply reviewed mapping");check(backend.profile[0].scale==.2 && !backend.profile[0].operatorNote.empty(),"operator clarification");check(!applyReviewedAction(proposal,base,QString(64,'a')).isEmpty(),"stale preview rejected");
    const QJsonObject plot{{"kind","plot"},{"signal","TEMP"}};check(applyReviewedAction(plot,desktop::profileToJson(backend.mappings()),QString(64,'a')).isEmpty() && plotRawColumn==-1 && page==7,"reviewed plot selection");
    // Identical conversion path on a recording beyond 24 hours, with both valid and incomplete inputs.
    const auto path=directory+"/fixture.atd";aero::Recorder recorder;check(recorder.open(std::filesystem::path(path.toStdWString())),"record open");auto config=backend.captureConfig;config.simulatorProfile=backend.mappings();config.enabled={true,true,true,true};
    std::vector<aero::Record> records;for(unsigned i=0;i<12;++i){auto r=aero::simulationRecord(config,i);r.timestampNs+=90000000000000ULL;records.push_back(r);}check(recorder.appendBatch(records),"record append");recorder.close();
    const auto report=desktop::batchAnalyze({path,directory+"/missing.atd"},directory,backend.mappings(),backend.captureConfig.checkRules);
    check(!report.contains("error") && report["completed_files"].toInt()==1 && report["failed_files"].toInt()==1,"batch partial failures");const auto result=report["results"].toArray()[0].toObject();check(result["engineering_rows"].toInt()==12 && result["integrity"]=="crc_valid","batch full range");
    QFile csv(result["csv"].toString());check(csv.open(QIODevice::ReadOnly) && csv.readAll().contains(",\"TEMP\",20,\"degC\",VALID,SIMULATED"),"engineering output");
    {
      aero::Signal route{"RT_TX","u"};route.rt=4;route.sa=7;route.transmit=true;
      aero::Record r;r.id=1;r.source=1;r.validity=15;r.messageType=2;r.commandWord=(3<<11)|(2<<5)|1;
      r.commandWord2=(4<<11)|(1<<10)|(7<<5)|1;r.statusWord=4<<11;r.statusWord2=3<<11;r.words={1234};
      const auto rtPath=directory+"/rt-to-rt.atd";aero::Recorder rtRecorder;
      check(rtRecorder.open(std::filesystem::path(rtPath.toStdWString()))&&rtRecorder.append(r)&&rtRecorder.close(),"RT-to-RT report fixture");
      const auto rtReport=desktop::batchAnalyze({rtPath},directory,{route},{});
      const auto rtResult=rtReport["results"].toArray().at(0).toObject();
      const auto stats=rtResult["signals"].toArray().at(0).toObject();
      check(rtResult["engineering_rows"].toInt()==1&&stats["valid"].toInt()==1&&std::abs(stats["mean"].toDouble()-12.34)<1e-9,"RT-to-RT batch export and statistics use payload transmitter");
    }
    std::atomic<bool> cancelled{true};const auto stopped=desktop::batchAnalyze({path},directory,backend.mappings(),{},&cancelled);check(stopped["cancelled"].toBool() && stopped["processed"].toInt()==0,"batch cancellation");
    backend.captureConfig.hardware=false;backend.captureConfig.runId=applicationRunId().toStdString();backend.captureConfig.simulatorRate=100;backend.captureConfig.simulatorProfile=backend.mappings();backend.captureConfig.checkRules={{"TEMP",0.,10.,100000000,0}};
    const auto livePath=directory+"/live-checks.atd";check(backend.startRecording(std::filesystem::path(livePath.toStdWString()),{true,true,true,true}),"start configured checks");
    QEventLoop capture;QTimer::singleShot(250,&capture,&QEventLoop::quit);capture.exec();backend.stopRecording();
    check(!backend.acquisition.metrics().failed && backend.acquisition.metrics().checkCount>0,"worker checks on generated payloads");QFile checkLog(livePath+".checks.jsonl");check(checkLog.open(QIODevice::ReadOnly) && checkLog.readAll().contains("\"kind\":\"range\""),"persistent check events");
    settings.setValue("eventRule3",true);navigate(9);const auto eventTable=content->findChild<QTableWidget *>("eventsTable");
    bool rangeVisible=false;if(eventTable)for(int row=0;row<eventTable->rowCount();++row)rangeVisible|=eventTable->item(row,1)&&eventTable->item(row,1)->text()=="range";
    check(rangeVisible,"recorded check events appear in Events page");
    taskCenter();check(taskDialog && taskDialog->findChild<QPushButton *>("apply"),"task controls");taskDialog->grab().save(directory+"/tasks.png");taskDialog->close();navigate(9);QCoreApplication::processEvents();grab().save(directory+"/events.png");
    desktop::writeJson(directory+"/acceptance.json",{{"passed",true},{"workspace",true},{"reviewed_mapping",true},{"reviewed_plot",true},{"batch_full_timestamp",true},{"partial_failure",true},{"cancellation",true},{"run_id",applicationRunId()},{"log_path",applicationLogPath()},{"preferences",settings.fileName()}});return true;
  }catch(const std::exception &e){desktop::writeJson(directory+"/acceptance.json",{{"passed",false},{"error",QString::fromUtf8(e.what())}});return false;}
}

