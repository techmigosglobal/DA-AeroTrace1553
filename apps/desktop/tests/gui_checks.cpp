#include "main_window.hpp"
#include <QTabBar>
#include <QScopeGuard>
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#include <QFile>
#include <QJsonDocument>
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
    for (int i : {2,3,4,6,7,10,11,12,14,16,17}) {
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
      report.write(passed ? "PASS: active Designer screens saved at 1500x940.\n" : "FAIL: a screen could not be rendered or saved.\n");
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
    check(unlocked && page == 2,
          "Application opens directly in Device Setup without a license gate");
    const auto mapping=desktop::signalToJson(aero::Signal{"TEMP","degC"});
    aero::Signal parsed;
    auto bad=mapping;bad["rt"]=3.5;
    check(!desktop::signalFromJson(bad,parsed).isEmpty(),"Fractional ICD addresses are rejected");
    auto legacy=mapping;legacy.remove("expected_word_count");aero::Signal legacySignal;
    check(desktop::signalFromJson(legacy,legacySignal).isEmpty() && legacySignal.expectedWordCount==0,"Older mapping keeps word count unspecified");
    auto counted=mapping;counted["expected_word_count"]=32;aero::Signal countedSignal;
    check(desktop::signalFromJson(counted,countedSignal).isEmpty() && desktop::signalToJson(countedSignal)["expected_word_count"].toInt()==32,"Word count survives JSON profile round trip");
    std::vector<aero::Signal> duplicate;
    auto duplicateObject=desktop::profileToJson({aero::Signal{"DUP","V"},aero::Signal{"DUP","A"}});
    check(!desktop::profileFromJson(duplicateObject,duplicate).isEmpty(),"Conflicting signal identifiers cannot activate");
    {
      const auto activeBefore=aero::serializeProfile(backend.profile);
      auto draftObject=desktop::profileToJson({aero::Signal{"DRAFT_TEMP","degC"}});
      draftObject["draft"]=true;
      const auto draftPath=reportDir+"/single-signal-draft.json";
      QFile draftFile(draftPath);
      const bool wrote=draftFile.open(QIODevice::WriteOnly|QIODevice::Truncate) && draftFile.write(QJsonDocument(draftObject).toJson())>0;
      draftFile.close();
      importIcdPath(draftPath);
      auto draftGrid=signalReviewDialog?signalReviewDialog->findChild<QTableWidget *>("signalGrid"):nullptr;
      check(wrote && draftGrid && draftGrid->rowCount()==1 && draftGrid->item(0,0)->text()=="DRAFT_TEMP" &&
        aero::serializeProfile(backend.profile)==activeBefore,"Saved ICD draft opens in the review grid without activating");
      if(signalReviewDialog){signalReviewDialog->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
      const QString csv="id,rt,sa,direction,word_count,data_word,lsb,width,representation,signed,word_order,scale,offset,unit,source_reference\n"
                        "\"CSV, signed\",4,2,receive,1,0,0,16,integer,true,msw-first,0.5,1,unitless,Revision A\n";
      reviewSignals({},csv,true);
      if(signalReviewDialog) {
        auto review=signalReviewDialog.data();
        auto grid=review->findChild<QTableWidget *>("signalGrid");
        grid->setCurrentCell(0,0);
        review->findChild<QLineEdit *>("hexWords")->setText("FFFE");
        review->findChild<QPushButton *>("previewHex")->click();
        check(grid->item(0,0)->text()=="CSV, signed" &&
          review->findChild<QLabel *>("reviewStatus")->text().contains("decoded -2") &&
          aero::serializeProfile(backend.profile)==activeBefore,"Quoted CSV stages signed engineering preview without activation");
        review->reject();
      } else check(false,"CSV review grid opened");

    }
    const auto originalSize = size();
    resize(1100, 600);
    for (int settle = 0; settle < 4; ++settle)
      QApplication::processEvents();
    for (int i = 0; i < nav.size(); ++i) {
      if (!nav[i] || nav[i]->isHidden()) continue;
      nav[i]->click();
      for (int settle = 0; settle < 3; ++settle)
        QApplication::processEvents();
      const auto route=(i==7)?6:(i==11)?10:i;
      const auto expectedTitle=route==4?QString("Monitor & Record"):route==6?QString("Engineering & Plots"):route==10?QString("Sessions & Replay"):route==12?QString("Signal Mappings"):names[route];
      check(page == i && headerTitle->text() == expectedTitle &&
                nav[route]->isChecked() && content->isVisible() &&
                pageLayout->count() > 0,
            "Navigation displays " + names[i]);
      int designerBodies = 0;
      for (auto widget : content->findChildren<QWidget *>())
        if (widget->property("designerSource").toString() == QString::fromUtf8(desktop::pages[i].form))
          ++designerBodies;
      check((i==1||i==2||i==3)?pageLayout->count()>0:designerBodies==1, "Native Qt screen instantiated for " + names[i]);
      auto bar = scroll->verticalScrollBar();
      bar->setValue(bar->maximum());
      QApplication::processEvents();
      const auto viewport=scroll->viewport();
      const bool overflows=scroll->widget()->height()>viewport->height()+1;
      const bool canScroll=!overflows||(bar->isVisible()&&bar->maximum()>0&&bar->value()==bar->maximum());
      const auto bottom=content->mapTo(viewport,QPoint(0,content->height())).y();
      check(canScroll&&bottom<=viewport->height()+1,
            "Bottom actions remain reachable in "+names[i]);
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
    settings.setValue("nextRunName","run2");
    startCapture();
    QEventLoop second;
    QTimer::singleShot(350, &second, &QEventLoop::quit);
    second.exec();
    stopAndSave();
    {QElapsedTimer wait;wait.start();while(savingSession&&wait.elapsed()<15000){QApplication::processEvents();QThread::msleep(5);}}
    check(!backend.acquisition.active(),
          "Stop & save stops acquisition cleanly");
    auto secondRun =
        aero::read(std::filesystem::path(currentFile.toStdWString()));
    check(secondRun.error.empty() && !secondRun.recovered,
          "Second capture leaves a readable finalized recording");
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
