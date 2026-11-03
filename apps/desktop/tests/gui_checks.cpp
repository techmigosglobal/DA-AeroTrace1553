#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

bool Window::smoke(const QString &dir){
    bool passed=true;
    backend.captureConfig.hardware=false;backend.profile.clear();
    findChild<QLabel *>("simulationBadge")->setText("SIMULATOR");
    for(int i=0;i<80;++i) model.records.push_back(aero::synthetic(i));model.refresh();
    backend.selected=model.records.front();
    resize(1500, 940);
    QDir().mkpath(dir);
    for (int i = 0; i < 18; ++i) {
      if (i == 1) {
        unlocked = true;
        licenseBadge->setText("KEY NOT VERIFIED");
        licenseBadge->setStyleSheet(
            "color:#008663;background:#edf9f4;border-radius:14px;padding:6px "
            "10px;font-size:10px");
        footerBadge->setText("● Not verified");
        footerBadge->setStyleSheet("color:#008663;font-size:11px");
      }
      navigate(i);
      for (int settle = 0; settle < 4; ++settle) {
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::processEvents();
      }
      const auto rendered=grab();
      passed=rendered.size()==QSize(1500,940) && rendered.save(dir + QString("/%1.png").arg(i + 1, 2, 10, QChar('0'))) && passed;
    }
    QFile report(dir + "/navigation.txt");
    if (report.open(QIODevice::WriteOnly))
      report.write(passed ? "PASS: all 18 Designer screens saved at 1500x940.\n" : "FAIL: a screen could not be rendered or saved.\n");
    else passed=false;
    return passed;
  }

bool Window::workflowTest(const QString &reportDir){
    backend.captureConfig.hardware=false;backend.profile.clear();
    findChild<QLabel *>("simulationBadge")->setText("SIMULATOR");
    QDir().mkpath(reportDir);
    QStringList results;
    bool passed = true;
    auto check = [&](bool ok, QString name) {
      results << (ok ? "PASS: " : "FAIL: ") + name;
      passed = passed && ok;
    };
    const auto mapping=desktop::signalToJson(aero::Signal{"TEMP","degC"});
    aero::Signal parsed;
    auto bad=mapping;bad["rt"]=3.5;
    check(!desktop::signalFromJson(bad,parsed).isEmpty(),"Fractional ICD addresses are rejected");
    std::vector<aero::Signal> duplicate;
    auto duplicateObject=desktop::profileToJson({aero::Signal{"DUP","V"},aero::Signal{"DUP","A"}});
    check(!desktop::profileFromJson(duplicateObject,duplicate).isEmpty(),"Conflicting signal identifiers cannot activate");
    const auto originalSize = size();
    resize(1100, 600);
    for (int settle = 0; settle < 4; ++settle)
      QApplication::processEvents();
    check(scroll->verticalScrollBar()->isVisible() &&
              scroll->verticalScrollBar()->maximum() > 0,
          "License page exposes a right scrollbar in a short window");
    scroll->verticalScrollBar()->setValue(
        scroll->verticalScrollBar()->maximum());
    for (int settle = 0; settle < 3; ++settle)
      QApplication::processEvents();
    bool exitReachable = false;
    for (auto b : content->findChildren<QPushButton *>())
      if (b->objectName() == "exitButton") {
        const auto bottom =
            b->mapTo(scroll->viewport(), QPoint(0, b->height())).y();
        exitReachable = bottom <= scroll->viewport()->height();
      }
    check(exitReachable,
          "Scrolling reaches the license page bottom action buttons");
    grab().save(reportDir + "/license-scrolled.png");
    navigate(4);
    check(page == 0, "Locked gate refuses monitor navigation");
    nav[3]->click();
    check(
        page == 0 && nav[0]->isChecked() && !nav[3]->isChecked(),
        "Locked sidebar click keeps the gate selected and remembers Dashboard");
    for (auto b : findChildren<QPushButton *>())
      if (b->objectName() == "primary") {
        b->click();
        break;
      }
    check(unlocked && page == 3,
          "Simulated unlock opens the requested Dashboard module");
    for (int i = 0; i < nav.size(); ++i) {
      nav[i]->click();
      for (int settle = 0; settle < 3; ++settle)
        QApplication::processEvents();
      check(page == i && headerTitle->text() == names[i] &&
                nav[i]->isChecked() && content->isVisible() &&
                pageLayout->count() > 0,
            "Sidebar click displays " + names[i]);
      int designerBodies = 0;
      for (auto widget : content->findChildren<QWidget *>())
        if (widget->property("designerSource").toString() == QString::fromUtf8(desktop::pages[i].form))
          ++designerBodies;
      check((i==1||i==2||i==3)?pageLayout->count()>0:designerBodies==1, "Native Qt screen instantiated for " + names[i]);
      auto bar = scroll->verticalScrollBar();
      bar->setValue(bar->maximum());
      QApplication::processEvents();
      const bool overflows = content->height() > scroll->viewport()->height();
      check(bar->isVisible() && (!overflows || bar->maximum() > 0) &&
                content->mapTo(scroll->viewport(), QPoint(0, content->height()))
                        .y() <= scroll->viewport()->height() + 1,
            "Right scrollbar reaches the bottom of " + names[i]);
    }
    resize(originalSize);
    QTemporaryDir dir;
    auto priorDir = sessionDir;
    sessionDir = dir.path();settings.setValue("exportFolder",dir.path()+"/exports");settings.setValue("nextRunName","run1");
    auto previous = settings.allKeys();
    QMap<QString, QVariant> saved;
    for (auto key : previous)
      saved[key] = settings.value(key);
    for (int ch = 1; ch <= 2; ++ch)
      for (int b = 0; b < 2; ++b)
        settings.setValue(QString("channel%1Bus %2").arg(ch).arg(b ? "B" : "A"),
                          ch == 1 && b == 0);
    startCapture();
    check(backend.acquisition.active(),
          "Start capture creates a local recording worker");
    displayPaused = true;
    auto visibleBefore = model.records.size();
    QEventLoop loop;
    QTimer::singleShot(1200, &loop, &QEventLoop::quit);
    loop.exec();
    check(backend.archive.size() > 1 && model.records.size() == visibleBefore,
          "Display pause preserves ongoing recording");
    toggleCapture();
    {QElapsedTimer wait;wait.start();while(savingSession&&wait.elapsed()<15000){QApplication::processEvents();QThread::msleep(5);}backend.accept(backend.acquisition.drain());}
    auto recorded =
        aero::read(std::filesystem::path(currentFile.toStdWString()));
    check(recorded.error.empty() && !recorded.recovered &&
              recorded.records == backend.archive,
          "Stop flushes exact CRC-valid capture records");
    check(std::all_of(recorded.records.begin(), recorded.records.end(),
                      [](auto &r) { return r.channel == 1 && r.bus == 0; }),
          "Acquisition respects enabled channel/bus settings");
    displayPaused = false;
    loadSession(currentFile);
    {QElapsedTimer wait;wait.start();while((page!=11||replaySeekPending||backend.replayRecords.empty())&&wait.elapsed()<15000){QApplication::processEvents();QThread::msleep(5);}}
    check(page == 11 && backend.replayRecords == backend.archive,
          "Session open restores identical replay records");
    if (backend.replayRecords.size() > 1) {
      seekReplay(1);
      {QElapsedTimer wait;wait.start();while(replaySeekPending&&wait.elapsed()<15000){QApplication::processEvents();QThread::msleep(5);}}
      check(backend.selected == backend.replayRecords[1] && scrub->value() == 1,
            "Replay seek synchronizes selected frame and scrubber");
    }
    model.records = backend.replayRecords;
    model.busFilter = "B";
    model.refresh();
    check(model.rowCount() == 0, "Bus filter excludes nonmatching records");
    model.busFilter = "";
    model.refresh();
    startCapture();
    QEventLoop second;
    QTimer::singleShot(350, &second, &QEventLoop::quit);
    second.exec();
    removeKey();
    check(!unlocked && !backend.acquisition.active() && page == 0,
          "Simulated key removal stops acquisition and locks all workspace "
          "navigation");
    auto interrupted =
        aero::read(std::filesystem::path(currentFile.toStdWString()));
    check(interrupted.error.empty() && !interrupted.recovered,
          "Key removal leaves a readable finalized recording");
    settings.clear();
    for (auto it = saved.begin(); it != saved.end(); ++it)
      settings.setValue(it.key(), it.value());
    settings.sync();
    sessionDir = priorDir;
    QFile report(reportDir + "/workflow-test.txt");
    if (report.open(QIODevice::WriteOnly))
      report.write(results.join('\n').toUtf8());
    return passed;
  }

bool Window::performanceTest(const QString &directory) {
  QMap<QString,QVariant> savedSettings;for(const auto &key:settings.allKeys()) savedSettings[key]=settings.value(key);
  QDir().mkpath(directory);
  const auto soakRoot=qEnvironmentVariable("AEROTRACE_SOAK_ROOT",QDir::tempPath());QDir().mkpath(soakRoot);
  QTemporaryDir temporary(QDir(soakRoot).filePath("aerotrace-soak-XXXXXX"));
  temporary.setAutoRemove(!qEnvironmentVariableIsSet("AEROTRACE_KEEP_SOAK"));
  if(!temporary.isValid())return false;
  const int duration=qBound(3,qEnvironmentVariableIntValue("AEROTRACE_SOAK_SECONDS")?qEnvironmentVariableIntValue("AEROTRACE_SOAK_SECONDS"):3,3600);
  if(duration>=60 && QStorageInfo(temporary.path()).bytesAvailable()<qint64(duration)*50000*1200)return false;
  sessionDir=temporary.path();unlocked=true;backend.captureConfig.hardware=false;backend.captureConfig.simulatorRate=50000;
  backend.profile.clear();
  const auto mappings=aero::Workbench::demoSignals();backend.applyProfile(mappings);
  for(int channel=1;channel<=2;++channel) for(const auto &bus:{"A","B"}) settings.setValue(QString("channel%1Bus %2").arg(channel).arg(bus),true);
  tick.setInterval(50);
  const auto path=std::filesystem::path((temporary.path()+"/performance.atd").toStdWString());
  backend.captureConfig.runId=applicationRunId().toStdString();currentFile=QString::fromStdWString(path.wstring());
  bool passed=backend.startRecording(path,{true,true,true,true});
  settings.setValue("monitorLayout","windows");navigate(4);
  QElapsedTimer clock;clock.start();qint64 last=clock.elapsed(),maximumGap=0;std::vector<qint64> gaps;int stage=0;
  QTimer layouts;layouts.setInterval(100);
  connect(&layouts,&QTimer::timeout,this,[&]{const int next=std::min(2,int(clock.elapsed()/(duration*1000./3)));if(next==stage)return;stage=next;if(stage==1){if(auto mode=content->findChild<QComboBox *>("workspaceLayout"))mode->setCurrentIndex(1);}else navigate(3);});layouts.start();
  QTimer heartbeat;heartbeat.setInterval(10);
  QJsonArray memorySamples;QTimer memory;memory.setInterval(5000);qint64 progressAt=0;
  auto sampleMemory=[&]{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),sizeof(m)))memorySamples.append(QJsonObject{{"elapsed_ms",clock.elapsed()},{"private_bytes",qint64(m.PrivateUsage)},{"working_set_bytes",qint64(m.WorkingSetSize)}});
#endif
    if(clock.elapsed()-progressAt>=60000){progressAt=clock.elapsed();const auto m=backend.acquisition.metrics(false);QSaveFile progress(directory+"/performance-progress.json");if(progress.open(QIODevice::WriteOnly)){progress.write(QJsonDocument(QJsonObject{{"stage","capturing"},{"elapsed_ms",clock.elapsed()},{"recorded",qint64(m.recorded)},{"display_skips",qint64(m.displayDrops)},{"queue_high_water",qint64(m.queueHighWater)},{"capture_failed",m.failed},{"log_error",applicationLogError()},{"memory",memorySamples.isEmpty()?QJsonValue():memorySamples.last()}}).toJson());progress.commit();}}
  };sampleMemory();connect(&memory,&QTimer::timeout,this,sampleMemory);memory.start();
  connect(&heartbeat,&QTimer::timeout,this,[&] {const auto now=clock.elapsed();maximumGap=std::max(maximumGap,now-last);gaps.push_back(now-last);last=now;});
  heartbeat.start();QEventLoop loop;QTimer::singleShot(duration*1000,&loop,&QEventLoop::quit);loop.exec();heartbeat.stop();layouts.stop();memory.stop();sampleMemory();
  const auto captureMilliseconds=clock.elapsed();
  backend.stopRecording();
  const auto metrics=backend.acquisition.metrics();const auto recorded=aero::scanRecording(path,[](const auto &,auto){return true;});
  const auto live=backend.acquisition.liveFrame();
  passed=passed && !metrics.failed && metrics.recorded>=125000 && recorded.count==metrics.recorded &&
    !recorded.recovered && recorded.error.empty() && metrics.queueHighWater<=8192 && live && live->records==metrics.recorded;
  if(live) for(const auto &trace:live->traces) passed=passed && trace.size()<=1024;
  std::sort(gaps.begin(),gaps.end());
  const auto p95=gaps.empty()?0:gaps[gaps.size()*95/100];passed=passed&&p95<100;
  const auto effectiveRate=metrics.recorded*1000.0/qMax<qint64>(1,captureMilliseconds);
  const bool rateMet=effectiveRate>=49000;passed=passed&&rateMet;
  qint64 warmMemory=0,peakWarm=0;for(auto sample:memorySamples){const auto s=sample.toObject();if(s["elapsed_ms"].toInteger()>=60000){const auto bytes=s["private_bytes"].toInteger();if(!warmMemory)warmMemory=bytes;peakWarm=qMax(peakWarm,bytes);}}
  const bool boundedMemory=!warmMemory||peakWarm-warmMemory<128ll*1024*1024;passed=passed&&boundedMemory;
  QJsonObject result{{"passed",passed},{"layouts_exercised","movable windows, tabs, RT dashboard"},{"p95_gui_heartbeat_ms",gaps.empty()?0:gaps[gaps.size()*95/100]},{"source","synthetic stress, no physical hardware"},{"requested_events_per_second",50000},
    {"recorded",qint64(metrics.recorded)},{"seconds",captureMilliseconds/1000.0},{"queue_high_water",qint64(metrics.queueHighWater)},
    {"display_skips",qint64(metrics.displayDrops)},{"max_gui_heartbeat_gap_ms",maximumGap},{"max_worker_batch_ms",metrics.maxBatchMilliseconds},
    {"plots",plots.size()},{"max_points_per_trace",1024},{"raw_crc_valid",!recorded.recovered && recorded.error.empty()},
    {"memory_samples",memorySamples},{"bounded_private_memory",boundedMemory},{"post_warmup_growth_bytes",peakWarm-warmMemory},
    {"recording",QString::fromStdWString(path.wstring())},{"capture_failed",metrics.failed},{"capture_error",QString::fromStdString(metrics.error)}};
  result["effective_records_per_second"]=effectiveRate;result["rate_within_two_percent"]=rateMet;
  if(duration>=60) {
    result["stage"]="capture_validated_exporting";
    QFile progress(directory+"/performance-progress.json");if(progress.open(QIODevice::WriteOnly)){progress.write(QJsonDocument(result).toJson());progress.close();}
    const auto file=QString::fromStdWString(path.wstring()),exports=temporary.path()+"/CSV";QDir().mkpath(exports);
    desktop::describeSession(file,backend.captureConfig);
    QFutureWatcher<desktop::SessionSaveResult> exportJob;QEventLoop exportWait;
    connect(&exportJob,&QFutureWatcher<desktop::SessionSaveResult>::finished,&exportWait,&QEventLoop::quit);
    exportJob.setFuture(QtConcurrent::run([file,exports]{return desktop::exportSession(file,exports,{},"soak");}));exportWait.exec();
    const auto saved=exportJob.result();
    auto countRows=[](const QString &p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return qint64(-1);qint64 count=-1;while(!f.atEnd()){const auto block=f.read(1024*1024);if(block.isEmpty()&&!f.atEnd())return qint64(-1);count+=block.count('\n');}return count;};
    const auto ext=countRows(exports+"/soak_EXT.csv"),in=countRows(exports+"/soak_INT.csv");
    const auto expectedExt=metrics.sourceCounts[0]+metrics.sourceCounts[1],expectedInt=metrics.sourceCounts[2]+metrics.sourceCounts[3];
    const bool exportComplete=saved.error.isEmpty()&&!saved.incomplete&&!saved.recovered&&ext==qint64(expectedExt)&&in==qint64(expectedInt)&&saved.external==expectedExt&&saved.internal==expectedInt;
    passed=passed&&exportComplete;result["export_complete"]=exportComplete;result["external_csv_rows"]=ext;result["internal_csv_rows"]=in;result["export_error"]=saved.error;result["export_manifest"]=saved.manifest;
  }
  const bool logHealthy=flushApplicationLogging()&&applicationLogError().isEmpty();passed=passed&&logHealthy;
  result["logging_healthy"]=logHealthy;result["logging_error"]=applicationLogError();result["application_log"]=applicationLogPath();
  result["passed"]=passed;result["stage"]="complete";
  QFile output(directory+"/performance.json");if(output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(result).toJson());else passed=false;
  settings.clear();for(auto it=savedSettings.begin();it!=savedSettings.end();++it) settings.setValue(it.key(),it.value());settings.sync();
  return passed;
}

bool Window::documentWorkflowTest(const QString &directory,const QString &pdf,unsigned expectedSignals) {
  QDir().mkpath(directory);unlocked=true;backend.profile.clear();
  resize(1500,940);setSidebarCollapsed(false);findChild<QLabel *>("simulationBadge")->setText("SIMULATOR");
  QTemporaryDir temporary;backend.captureConfig.hardware=false;backend.captureConfig.simulatorRate=expectedSignals==9 ? 120 : 50000;
  const auto capturePath=std::filesystem::path((temporary.path()+"/integrated.atd").toStdWString());
  const auto captureStarted=backend.startRecording(capturePath,{true,true,true,true});
  QStringList results;bool passed=true;
  auto check=[&](bool ok,const QString &name) {results<<(ok ? "PASS: " : "FAIL: ")+name;passed=passed && ok;};
  auto wait=[&](const std::function<bool()> &ready,int milliseconds) {
    QEventLoop loop;QTimer poll;poll.setInterval(25);
    connect(&poll,&QTimer::timeout,&loop,[&]{if(ready()) loop.quit();});poll.start();
    QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();return ready();
  };
  documentCenter(pdf);
  const auto imported=wait([&] {return documentDialog && documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && backend.profile.size()==expectedSignals;},60000);
  check(imported,QString("PDF worker protocol activates %1 validated mappings in the GUI").arg(expectedSignals));
  if(documentDialog && imported) {
    const auto status=documentDialog->findChild<QLabel *>("status");
    results<<"Import status: "+status->text();
    documentDialog->grab().save(directory+"/document-import.png");
    documentDialog->findChild<QLineEdit *>("question")->setText("Temperature TEMP engineering unit");
    documentDialog->findChild<QPushButton *>("askButton")->click();
    const auto answered=wait([&] {return documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QPlainTextEdit *>("answer")->toPlainText().contains("degC");},60000);
    check(answered,"GUI retrieval answer contains the imported ICD evidence and engineering unit");
    if(expectedSignals==9 && answered) {
      const auto profileBefore=aero::serializeProfile(backend.profile);
      const auto selected=documentDialog->findChild<QComboBox *>("documentSelector")->currentData().toString();
      documentDialog->findChild<QLineEdit *>("question")->setText("What is its scale?");documentDialog->findChild<QPushButton *>("askButton")->click();
      const auto followed=wait([&]{return documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QPlainTextEdit *>("answer")->toPlainText().contains("scale 0.01");},60000);
      check(followed && profileBefore==aero::serializeProfile(backend.profile),"ICD chat resolves a follow-up without changing approved engineering rules");
      check(documentDialog->windowModality()==Qt::NonModal && !selected.isEmpty(),"ICD chat is a separate modeless window scoped to the selected document");
      documentDialog->grab().save(directory+"/icd-chat.png");
      documentDialog->close();wait([&]{return documentDialog.isNull();},2000);documentCenter();
      const auto restored=wait([&]{return documentDialog && documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QComboBox *>("documentSelector")->currentData().toString()==selected && documentDialog->findChild<QPlainTextEdit *>("answer")->toPlainText().contains("What is its scale?");},15000);
      check(restored && profileBefore==aero::serializeProfile(backend.profile),"Reopening ICD Assistant restores the selected ICD and persisted conversation without reindexing or activating another profile");
    }
  }
  if(imported) {
    QEventLoop capture;QTimer::singleShot(450,&capture,&QEventLoop::quit);capture.exec();
    // The demo deliberately injects protocol flags, which invalidate that instant's value.
    const auto decoded=wait([&]{return backend.latest(backend.profile[0],{}).has_value();},2000);
    check(captureStarted && decoded,"Imported PDF rule drives worker-side engineering conversion");
    navigate(7);check(!plots.empty() && plots.front()->pointCount()>0,"Converted live samples reach the Qwt plot");
    if(expectedSignals==9) {
      preferredSignalIndex=8;analysisChannel=2;analysisBus=1;plotRawColumn=-1;navigate(7);
      const auto subscribed=wait([&]{const auto frame=backend.acquisition.liveFrame();return frame && std::find(frame->traceSignals.begin(),frame->traceSignals.end(),8)!=frame->traceSignals.end();},2000);
      auto retained=std::move(backend.archive);backend.archive.clear();const auto workerSeries=backend.series("",8,{},2,1);backend.archive=std::move(retained);
      check(subscribed && !workerSeries.primary.empty() && !plots.empty() && plots.front()->pointCount()>0,"Ninth ICD signal has worker-owned Channel 2 / Bus B history independent of the retained raw display");
      navigate(6);const auto ready=wait([&]{return qualityTable && qualityTable->rowCount()==9 && qualityTable->item(8,4)->text()=="VALID";},2000);
      check(ready,"Live engineering table includes every mapping with raw words and microsecond card time");
      grab().save(directory+"/demo-engineering.png");navigate(7);grab().save(directory+"/demo-plot.png");
    }
  }
  backend.stopRecording();
  const auto metrics=backend.acquisition.metrics();
  for(const QString suffix:{".events.jsonl",".status.txt"}) {
    const auto target=directory+"/capture"+suffix;QFile::remove(target);
    check(QFile::copy(QString::fromStdWString(capturePath.wstring())+suffix,target),"Current capture diagnostics copied: "+suffix);
  }
  check(captureStarted && !metrics.failed && metrics.recorded>(expectedSignals==9 ? 100u : 50000u),"Capture continues while PDF extraction, embedding and retrieval run");
  results<<QString("Concurrent capture: %1 records, %2 display skips, queue high-water %3").arg(metrics.recorded).arg(metrics.displayDrops).arg(metrics.queueHighWater);
  if(expectedSignals==9) {
    const auto loaded=backend.loadRecording(capturePath);if(backend.replayCount()) backend.seek(backend.replayCount()-1);
    const auto offline=backend.liveValue(8,{},2,1);const auto offlinePlot=backend.series("",8,{},2,1);
    check(loaded.error.empty() && offline.value.has_value() && !offlinePlot.primary.empty(),"The selected ICD also converts and plots a completed recording after simulation");
  }
  if(imported && documentDialog) {
    const auto before=aero::serializeProfile(backend.profile);
    documentDialog->findChild<QLineEdit *>("question")->setText("Set TEMP scale to 0.02");documentDialog->findChild<QPushButton *>("previewAction")->click();
    const auto previewed=wait([&]{return documentDialog->findChild<QPushButton *>("applyAction")->isEnabled();},15000);
    check(previewed && before==aero::serializeProfile(backend.profile),"Natural-language clarification previews without mutating approved mappings");
    if(previewed){documentDialog->grab().save(directory+"/reviewed-action.png");documentDialog->findChild<QPushButton *>("applyAction")->click();check(backend.profile[0].scale==.02 && !backend.profile[0].operatorNote.empty(),"Reviewed clarification reaches the C++ conversion profile with operator provenance");}
  }
  if(documentDialog) documentDialog->close();
  QFile output(directory+"/document-workflow.txt");
  if(output.open(QIODevice::WriteOnly)) output.write(results.join('\n').toUtf8());else passed=false;
  return passed;
}
