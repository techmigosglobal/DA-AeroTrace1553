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
                        "\"CSV, signed\",4,2,receive,1,1,0,16,integer,true,msw-first,0.5,1,unitless,Revision A\n";
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
      const auto tabIndex=i==5?0:i==8?1:i==9?2:-1;
      const auto tabs=content->findChild<QTabBar *>("busAnalysisTabs");
      const bool workspace=i==6||i==7||i==12;
      const auto expectedTitle=workspace?QString("ICD & Analysis"):tabIndex>=0?QString("Bus Analysis"):names[i];
      check(page == i && headerTitle->text() == expectedTitle &&
                (tabIndex<0 || (tabs && tabs->currentIndex()==tabIndex)) &&
                nav[workspace?12:i]->isChecked() && content->isVisible() &&
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

bool Window::documentWorkflowTest(const QString &directory,const QString &pdf,unsigned expectedSignals) {
  if(expectedSignals==54) {
    QDir().mkpath(directory);
    const auto overridesPath=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents/review-edits.ini";
    QFile overrides(overridesPath);const bool hadOverrides=overrides.exists();QByteArray oldOverrides;
    if(overrides.open(QIODevice::ReadOnly)){oldOverrides=overrides.readAll();overrides.close();}QFile::remove(overridesPath);
    const auto restoreEdits=qScopeGuard([&]{if(hadOverrides){QSaveFile file(overridesPath);if(file.open(QIODevice::WriteOnly)){file.write(oldOverrides);file.commit();}}else QFile::remove(overridesPath);});
    unlocked=true;
    backend.profile.clear();workspaceSubsystem.clear();settings.remove("workspace/live/subsystem");
    QStringList results;
    bool passed=true;
    const auto check=[&](bool ok,const QString &message) {
      results.append((ok?"PASS: ":"FAIL: ")+message);
      passed&=ok;
    };
    const auto wait=[&](const std::function<bool()> &ready) {
      QEventLoop loop;
      QTimer poll;
      connect(&poll,&QTimer::timeout,&loop,[&]{if(ready())loop.quit();});
      poll.start(25);
      QTimer::singleShot(60000,&loop,&QEventLoop::quit);
      loop.exec();
      return ready();
    };
    documentCenter(pdf);
    const auto indexed=wait([&]{return documentDialog && documentDialog->findChild<QTableWidget *>("mappingTable")->rowCount()==57;});
    check(indexed,"Indexed 54 typed rows and three evidence-only rows");
    if(indexed)documentDialog->findChild<QPushButton *>("activateButton")->click();
    check(signalReviewDialog!=nullptr,"Opened Table-1 review");
    if(auto review=signalReviewDialog.data()) {
      const auto grid=review->findChild<QTableWidget *>("signalGrid");
      check(grid->rowCount()==54 && grid->item(0,0)->text()=="Subsystem-1 / 04102 / ID_1",
            "Preserved qualified signal IDs and staged only typed fields");
      grid->item(0,11)->setText(QString());
      review->findChild<QPushButton *>("publish")->click();
      check(backend.profile.empty(),"Unconfirmed conversion fields block publication");
      grid->item(1,11)->setText("7");
      bool presetPreview=false;
      QTimer::singleShot(0,review,[&presetPreview] {
        const auto settingsDialog=QApplication::activeModalWidget();
        if(!settingsDialog)return;
        const auto order=settingsDialog->findChild<QComboBox *>("tableFloatOrder");
        const auto identity=settingsDialog->findChild<QCheckBox *>("tableIdentityScale");
        if(!order || !identity){settingsDialog->close();return;}
        order->setCurrentIndex(1);
        identity->setChecked(true);
        for(auto channel:settingsDialog->findChildren<QComboBox *>("tableChannel")) {
          const auto group=channel->property("subsystem").toString();
          if(group=="Subsystem-1")channel->setCurrentIndex(1);
          if(group=="Subsystem-3")channel->setCurrentIndex(2);
        }
        presetPreview=settingsDialog->findChild<QTableWidget *>("presetPreview")->rowCount()>0;
        settingsDialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
      });
      review->findChild<QPushButton *>("tableSettings")->click();
      check(presetPreview && grid->item(1,11)->text()=="7","Bulk preset previews affected cells and preserves manual scaling");
      grid->item(1,11)->setText("1");
      grid->setCurrentCell(0,0);
      review->findChild<QLineEdit *>("hexWords")->setText("3F80 0000");
      review->findChild<QPushButton *>("previewHex")->click();
      check(review->findChild<QLabel *>("reviewStatus")->text().contains("= 1 Rad"),"MSW-first float preview decodes 1 Rad");
      review->findChild<QPushButton *>("publish")->click();
    }
    check(backend.profile.size()==54,"Published all typed rows with confirmed synthetic test settings");
    if(backend.profile.size()==54) {
      check(backend.profile[0].subsystem=="Subsystem-1" && backend.profile[36].subsystem=="Subsystem-3" && backend.profile[36].sourceSignalId=="ID_39" && backend.profile[36].messageId=="04102","Profile retains subsystem, message and source signal metadata");
      std::vector<aero::Signal> roundtrip;
      check(desktop::profileFromJson(QJsonDocument::fromJson(QByteArray::fromStdString(aero::serializeProfile(backend.profile))).object(),roundtrip).isEmpty() && roundtrip[36].subsystem=="Subsystem-3","Session snapshot serialization round-trips optional subsystem metadata");
      const auto ask=[&](QString question,QString expected){
        const auto button=documentDialog->findChild<QPushButton *>("askButton");
        if(!wait([&]{return button->isEnabled();}))return false;
        documentDialog->findChild<QComboBox *>("answerSource")->setCurrentIndex(0);
        documentDialog->findChild<QLineEdit *>("question")->setText(question);button->click();
        return wait([&]{return button->isEnabled() && documentDialog->findChild<QTextBrowser *>("answer")->toPlainText().contains(expected);});
      };
      check(ask("Which words are float?","39 matching float fields"),"Complete ICD answer contains all 39 float rows with pagination");
      workspaceSubsystem="Subsystem-3";workspaceScopeKey="live";settings.setValue("workspace/live/subsystem",workspaceSubsystem);analysisChannel=2;preferredSignalIndex=36;navigate(6);
      check(qualityTable && qualityTable->isRowHidden(0) && !qualityTable->isRowHidden(36),"Engineering filters the selected subsystem and CH2 route");
      check(documentDialog->findChild<QComboBox *>("assistantScope")->currentData().toString().isEmpty(),"Existing conversation retains its original subsystem scope");
      const auto newChat=documentDialog->findChild<QPushButton *>("newConversation");newChat->click();
      check(wait([&]{return documentDialog->findChild<QPushButton *>("askButton")->isEnabled();})&&ask("Which words are float?","13 matching float fields in Subsystem-3"),"New conversation inherits the workspace subsystem and returns all 13 fields");
      if(documentDock){documentDialog->findChild<QPushButton *>("expandAssistant")->click();}documentDialog->show();QApplication::processEvents();documentDialog->grab().save(directory+"/assistant-workspace.png");
      if(documentDock)documentDock->hide();navigate(12);QApplication::processEvents();grab().save(directory+"/mapping-workspace.png");navigate(6);QApplication::processEvents();grab().save(directory+"/engineering-workspace.png");navigate(7);QApplication::processEvents();grab().save(directory+"/plot-workspace.png");
      check(findChild<QComboBox *>("panel0Select") && findChild<QComboBox *>("panel0Select")->count()==19,"Plot selections share the selected subsystem");
      workspaceSubsystem.clear();settings.remove("workspace/live/subsystem");
      aero::Record record;
      record.id=1;
      record.channel=1;
      record.commandWord=(4<<11)|(1<<10)|(2<<5);
      record.statusWord=4<<11;
      record.words.resize(32);
      record.words[0]=0x3f80;
      record.words[26]=12;
      check(aero::decode(record,backend.profile[0])==1 && aero::decode(record,backend.profile[13])==12 &&
        !aero::decode(record,backend.profile[36]),"CH1 matches only Subsystem 1 and decodes unsigned 16-bit data");
      record.channel=2;
      record.words[0]=0xbf00;
      check(!aero::decode(record,backend.profile[0]) && aero::decode(record,backend.profile[36])==-.5,
            "CH2 matches Subsystem 3 with the same bus route and decodes negative float");
#ifdef AEROTRACE_QWT
      Plot plot;
      plot.setSeries({{0,0.5},{1,std::numeric_limits<double>::quiet_NaN()},{2,1.5}}, {}, {}, 1.2);
      const auto graph=plot.findChild<QwtPlot *>();
      const auto items=graph?graph->itemList(QwtPlotItem::Rtti_PlotCurve):QwtPlotItemList();
      const auto excursion=std::find_if(items.begin(),items.end(),[](const auto *item){
        return item->title().text()=="Outside configured limits";
      });
      const auto curve=excursion==items.end()?nullptr:dynamic_cast<QwtPlotCurve *>(*excursion);
      check(curve && curve->dataSize()==1 && curve->sample(0)==QPointF(2,1.5) && plot.pointCount()==2,
            "Qwt marks only the out-of-limit sample and preserves the data gap");
#endif
      if(documentDialog) {
        documentDialog->findChild<QComboBox *>("answerSource")->setCurrentIndex(1);
        documentDialog->findChild<QLineEdit *>("question")->setText("ID_39 encoding");
        const auto ask=documentDialog->findChild<QPushButton *>("askButton");
        const auto available=wait([&]{return ask->isEnabled();});
        if(available)ask->click();
        check(available && wait([&]{
          const auto answer=documentDialog->findChild<QTextBrowser *>("answer")->toPlainText();
          return answer.contains("Published mappings (Engineering)") && answer.contains("CH2") &&
                 answer.contains("MSW first") && answer.contains("Table-1.pdf, page 3");
        }),"Published chat reports the confirmed CH2 rule and cites the correct PDF page");
      }
    }
    if(backend.profile.size()==54) {
      const auto before=desktop::profileToJson(backend.profile);
      reviewSignals();
      if(signalReviewDialog){auto grid=signalReviewDialog->findChild<QTableWidget *>("signalGrid");
        signalReviewDialog->findChild<QComboBox *>("reviewSubsystem")->setCurrentText("Subsystem-3");grid->item(36,11)->setText("2");signalReviewDialog->findChild<QPushButton *>("publish")->click();
        check(backend.profile.size()==54 && backend.profile[36].scale==2 && desktop::signalToJson(backend.profile[0])==before["signals"].toArray()[0].toObject(),"Publishing one subsystem merges its edits and preserves other groups");
      }
      std::vector<aero::Signal> replay;desktop::profileFromJson(before,replay);backend.replayProfile=replay;
      check(backend.replayProfile[36].scale==1 && backend.profile[36].scale==2,"Replay retains its own profile revision after live publication");
    }
    if(backend.profile.size()==54) {
      const auto published=backend.profile;backend.profile.clear();workspaceSubsystem.clear();settings.remove("workspace/live/subsystem");
      if(signalReviewDialog)signalReviewDialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);wait([&]{return signalReviewDialog.isNull();});
      reviewDocument(workspaceDocument,workspacePdf);
      if(signalReviewDialog){auto grid=signalReviewDialog->findChild<QTableWidget *>("signalGrid");
        check(grid->rowCount()==54 && grid->item(0,11)->text()=="1" && grid->item(0,10)->text()=="msw-first","Re-import reuses reviewed presets and exact manual overrides");
        grid->item(0,11)->setText("3");signalReviewDialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);wait([&]{return signalReviewDialog.isNull();});
      }
      backend.applyProfile(published);const auto beforeReimport=aero::serializeProfile(backend.profile);
      auto changed=workspaceDocument;auto review=changed["review"].toArray();auto first=review[0].toObject();auto fields=first["draft_cells"].toObject();fields["data_word"]=fields["data_word"].toInt()+1;first["draft_cells"]=fields;first["text"]=first["text"].toString()+" changed source word";review[0]=first;for(int row=0;row<review.size();++row){auto data=review[row].toObject();data["source_hash"]=QString(64,'e');review[row]=data;}changed["review"]=review;
      reviewDocument(changed,workspacePdf);
      if(signalReviewDialog){auto grid=signalReviewDialog->findChild<QTableWidget *>("signalGrid");
        check(grid->rowCount()==54 && grid->item(0,11)->text()=="3" && grid->item(36,11)->text()=="2" && grid->item(0,0)->data(Qt::UserRole).toJsonObject()["definition_changed"].toBool(),"Changed re-import preserves manual scaling and flags the changed definition");
        signalReviewDialog->findChild<QPushButton *>("publish")->click();check(aero::serializeProfile(backend.profile)==beforeReimport,"Changed definitions block publication and retain the active profile until explicitly accepted");signalReviewDialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);wait([&]{return signalReviewDialog.isNull();});
      }
      backend.applyProfile(published);
    }
    if(documentDialog && QCoreApplication::arguments().contains("--exercise-model")) {
      workspaceSubsystem="Subsystem-3";workspaceScopeKey="live";analysisChannel=2;navigate(6);
      const auto button=documentDialog->findChild<QPushButton *>("askButton");wait([&]{return button->isEnabled();});
      documentDialog->findChild<QComboBox *>("answerSource")->setCurrentIndex(0);
      documentDialog->findChild<QLineEdit *>("question")->setText("Summarize the Rad and m/sec fields documented here.");
      QElapsedTimer timer;timer.start();button->click();bool modelStarted=false;
      QEventLoop loop;QTimer poll;poll.setInterval(100);
      connect(&poll,&QTimer::timeout,&loop,[&]{modelStarted|=inference.processId()>0;if(button->isEnabled())loop.quit();});poll.start();QTimer::singleShot(240000,&loop,&QEventLoop::quit);loop.exec();
      const auto mode=documentDialog->property("lastAnswerMode").toString();
      check(modelStarted && button->isEnabled() && (mode=="local-model" || mode=="extractive") && documentDialog->property("lastCitationCount").toInt()>0,"Managed assistant completes a real scoped evidence question with generation or cited fallback");
      results<<QString("Scoped RAG: %1 ms · mode %2 · %3 streamed events").arg(timer.elapsed()).arg(mode).arg(documentDialog->property("answerDeltaCount").toInt());
    }
    if(documentDialog)documentDialog->close();
    QFile output(directory+"/table-icd-workflow.txt");
    if(!output.open(QIODevice::WriteOnly))return false;
    output.write(results.join('\n').toUtf8());
    return passed;
  }
  QDir().mkpath(directory);unlocked=true;backend.profile.clear();
  resize(1500,940);setSidebarCollapsed(false);findChild<QLabel *>("simulationBadge")->setText("SIMULATOR");
  QTemporaryDir temporary;backend.captureConfig.hardware=false;backend.captureConfig.simulatorRate=expectedSignals>=9 ? 120 : 50000;
  const auto savedSource=settings.value("captureSource");
  bool migrated=true;
  for(const auto *source:{"EXAMPLE","MAPPED_ICD"}) {
    settings.setValue("captureSource",source);
    restoreCaptureSource();
    migrated&=!backend.captureConfig.hardware && settings.value("captureSource")=="SIMULATOR";
  }
  if(savedSource.isValid()) settings.setValue("captureSource",savedSource);
  else settings.remove("captureSource");
  settings.sync();
  const auto capturePath=std::filesystem::path((temporary.path()+"/integrated.atd").toStdWString());
  const auto captureStarted=backend.startRecording(capturePath,{true,true,true,true});
  QStringList results;bool passed=true;
  auto check=[&](bool ok,const QString &name) {results<<(ok ? "PASS: " : "FAIL: ")+name;passed=passed && ok;};
  check(migrated,"Retired simulator settings restore the single Offline simulation source");
  auto wait=[&](const std::function<bool()> &ready,int milliseconds) {
    QEventLoop loop;QTimer poll;poll.setInterval(25);
    connect(&poll,&QTimer::timeout,&loop,[&]{if(ready()) loop.quit();});poll.start();
    QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();return ready();
  };
  documentCenter(pdf);
  const auto ready=wait([&] {return documentDialog && documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QTableWidget *>("mappingTable")->rowCount()>=int(expectedSignals);},60000);
  check(ready && backend.profile.empty(),"PDF import requires explicit mapping review");
  if(ready) {
    auto table=documentDialog->findChild<QTableWidget *>("mappingTable");
    for(int i=0;i<table->rowCount();++i){auto item=table->item(i,0);if(item && (item->flags()&Qt::ItemIsUserCheckable))item->setCheckState(Qt::Checked);}
    documentDialog->findChild<QPushButton *>("activateButton")->click();
    if(signalReviewDialog) {
      auto review=signalReviewDialog.data();
      if(expectedSignals==18) {
        auto grid=review->findChild<QTableWidget *>("signalGrid");
        // Re-import can restore edits from a previous run. Make this check start unresolved.
        grid->item(0,11)->setText(QString());
        review->findChild<QPushButton *>("publish")->click();
        check(backend.profile.empty() && grid->rowCount()==18,"Unresolved PDF fields cannot publish");
        review->findChild<QPushButton *>("demoAssumptions")->click();
        grid->setCurrentCell(0,0);
        review->findChild<QLineEdit *>("hexWords")->setText("3F80 0000");
        review->findChild<QPushButton *>("previewHex")->click();
        check(review->findChild<QLabel *>("reviewStatus")->text().contains("decoded 1"),"PDF example uses reviewed IEEE float32 word order");
      }
      if(expectedSignals==9) {
        review->findChild<QPushButton *>("publish")->click();
        check(backend.profile.empty() && review->findChild<QLabel *>("reviewStatus")->text().contains("Overlapping field"),
          "Full-word and discrete overlaps require review");
        review->findChild<QCheckBox *>("reviewOverlap")->setChecked(true);
      }
      review->findChild<QPushButton *>("publish")->click();
    }
  }
  const auto imported=ready && backend.profile.size()==expectedSignals;
  check(imported,QString("Reviewed PDF rules activate %1 mappings in the GUI").arg(expectedSignals));
  if(documentDialog && imported) {
    const auto status=documentDialog->findChild<QLabel *>("status");
    results<<"Import status: "+status->text();
    documentDialog->grab().save(directory+"/document-import.png");
    documentDialog->findChild<QLineEdit *>("question")->setText(expectedSignals==18?"What is the RT, subaddress and word count?":expectedSignals==19?"RT03_WORD_01 engineering unit":"Temperature TEMP engineering unit");
    documentDialog->findChild<QPushButton *>("askButton")->click();
    const auto answered=wait([&] {return documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QTextBrowser *>("answer")->toPlainText().contains(expectedSignals==18?"subaddress 12, receive, 32 data words":expectedSignals==19?"km/hr":"degC");},60000);
    check(answered,"GUI retrieval answer contains the imported ICD evidence and engineering unit");
    if(expectedSignals==9 && answered) {
      const auto profileBefore=aero::serializeProfile(backend.profile);
      const auto selected=documentDialog->findChild<QComboBox *>("documentSelector")->currentData().toString();
      documentDialog->findChild<QLineEdit *>("question")->setText("What is its scale?");documentDialog->findChild<QPushButton *>("askButton")->click();
      const auto followed=wait([&]{return documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QTextBrowser *>("answer")->toPlainText().contains("scale 0.01");},60000);
      check(followed && profileBefore==aero::serializeProfile(backend.profile),"ICD chat resolves a follow-up without changing approved engineering rules");
      check(documentDialog->windowModality()==Qt::NonModal && !selected.isEmpty(),"ICD chat is a separate modeless window scoped to the selected document");
      documentDialog->grab().save(directory+"/icd-chat.png");
      documentDialog->close();wait([&]{return documentDialog.isNull();},2000);documentCenter();
      const auto restored=wait([&]{return documentDialog && documentDialog->findChild<QPushButton *>("askButton")->isEnabled() && documentDialog->findChild<QComboBox *>("documentSelector")->currentData().toString()==selected && documentDialog->findChild<QTextBrowser *>("answer")->toPlainText().contains("What is its scale?");},15000);
      check(restored && profileBefore==aero::serializeProfile(backend.profile),"Reopening ICD Assistant restores the selected ICD and persisted conversation without reindexing or activating another profile");
    }
  }
  const auto importedMappings=backend.profile;
  if(imported && expectedSignals!=18 && expectedSignals!=19) {
    auto testMappings=importedMappings;
    for(auto &mapping:testMappings) {
      mapping.rt=3;mapping.sa=12;mapping.transmit=false;mapping.expectedWordCount=32;
    }
    check(backend.applyProfile(std::move(testMappings)).empty(),"Reviewed rules can be applied to the test fixture route");
  }
  if(imported) {
    QEventLoop capture;QTimer::singleShot(450,&capture,&QEventLoop::quit);capture.exec();
    // The demo deliberately injects protocol flags, which invalidate that instant's value.
    const auto decoded=wait([&]{return backend.latest(backend.profile[0],{}).has_value();},2000);
    check(captureStarted && decoded,"Imported PDF rule drives worker-side engineering conversion");
    navigate(7);check(!plots.empty() && plots.front()->pointCount()>0,"Converted live samples reach the Qwt plot");
    if(expectedSignals==19) {
      const auto rt8=wait([&]{return backend.latest(backend.profile.back(),{}).has_value();},2000);
      check(rt8 && backend.profile.back().simulationOnly,"Published fixture converts both RTs with simulation-only provenance");
      plotPanelIds[3]=QString::fromStdString(backend.profile.back().id);navigate(7);
      check(wait([&]{return plots.size()==4 && plots.back()->pointCount()>0;},2500),"RT 08 reaches the fourth plot panel");
      check(signalReportWorkflowTest(directory),"Both fixture terminals export with exact route matching");
    }
    if(expectedSignals==18) {
      check(backend.profile.size()==18 && std::all_of(backend.profile.begin(),backend.profile.end(),[](const auto &rule){return rule.simulationOnly && rule.expectedWordCount==32 && rule.word<30;}),"18 reviewed mappings exclude reserved words and require example context");
      const auto displayed=wait([&]{return plots.size()==4 && std::all_of(plots.begin(),plots.end(),[](auto plot){return plot->pointCount()>0;});},3000);
      check(displayed,"All four plot panels receive 32-word demo samples");
      check(signalReportWorkflowTest(directory),"Live and full-session reports use the same decoding and publish CSV, PNG and manifest");
      navigate(3);
      check(selectedRt==-1 && rtDetail && rtDetail->text().startsWith("Select"),"Dashboard initially has no selected terminal");
      navigate(5);
      check(findChild<QTabBar *>("busAnalysisTabs")!=nullptr,"Inspector opens within Bus Analysis");
    }
    if(expectedSignals==9) {
      preferredSignalIndex=8;plotPanelIds[0]=QString::fromStdString(backend.profile[8].id);analysisChannel=2;analysisBus=1;plotRawColumn=-1;navigate(7);
      const auto subscribed=wait([&]{const auto frame=backend.acquisition.liveFrame();return frame && std::find(frame->traceSignals.begin(),frame->traceSignals.end(),8)!=frame->traceSignals.end();},2000);
      auto retained=std::move(backend.archive);backend.archive.clear();const auto workerSeries=backend.series("",8,{},2,1);backend.archive=std::move(retained);
      check(subscribed && !workerSeries.primary.empty() && !plots.empty() && plots.front()->pointCount()>0,"Ninth ICD signal has worker-owned Channel 2 / Bus B history independent of the retained raw display");
      navigate(6);const auto ready=qualityTable && qualityTable->rowCount()==9 && content->findChild<QPlainTextEdit *>("previewResult");
      check(ready,"Engineering lists every stored mapping and exposes native conversion preview");
      const auto &records=backend.data(model.records);const auto selected=std::find_if(records.rbegin(),records.rend(),[&](const auto &r){return r.channel==2&&r.bus==1&&aero::decode(r,backend.profile[8],backend.exampleContext()).has_value();});
      if(selected!=records.rend()){backend.selected=*selected;content->findChild<QPushButton *>("useRecord")->click();const auto expected=aero::decode(*selected,backend.profile[8],backend.exampleContext());const auto actual=content->findChild<QPlainTextEdit *>("previewResult")->property("decodedValue");check(actual.isValid()&&expected&&actual.toDouble()==*expected,"Selected-record preview agrees with native live/replay conversion");}else check(false,"A matching saved record exists for conversion preview");
      grab().save(directory+"/demo-engineering.png");navigate(7);grab().save(directory+"/demo-plot.png");
    }
  }
  backend.stopRecording();
  const auto metrics=backend.acquisition.metrics();
  for(const QString suffix:{".events.jsonl",".status.txt"}) {
    const auto target=directory+"/capture"+suffix;QFile::remove(target);
    check(QFile::copy(QString::fromStdWString(capturePath.wstring())+suffix,target),"Current capture diagnostics copied: "+suffix);
  }
  check(captureStarted && !metrics.failed && metrics.recorded>(expectedSignals>=9 ? 100u : 50000u),"Capture continues while PDF extraction, embedding and retrieval run");
  results<<QString("Concurrent capture: %1 records, %2 display skips, queue high-water %3").arg(metrics.recorded).arg(metrics.displayDrops).arg(metrics.queueHighWater);
  if(expectedSignals==9) {
    const auto loaded=backend.loadRecording(capturePath);backend.replayProfile=backend.profile;if(backend.replayCount()) backend.seek(backend.replayCount()-1);
    const auto offline=backend.liveValue(8,{},2,1);const auto offlinePlot=backend.series("",8,{},2,1);
    check(loaded.error.empty() && offline.value.has_value() && !offlinePlot.primary.empty(),"The selected ICD also converts and plots a completed recording after simulation");
  }
  if((expectedSignals==18 || expectedSignals==19) && imported) {
    const auto original=aero::serializeProfile(backend.profile);
    loadSession(QString::fromStdWString(capturePath.wstring()));
    const auto loaded=wait([&]{return backend.replaySource && backend.replayExample && backend.replayProfile.size()==expectedSignals && !backend.replayRecords.empty();},10000);
    const auto value=loaded?backend.liveValue(0,{},1,0):aero::LiveValue{};
    check(loaded && value.value.has_value() && aero::serializeProfile(backend.profile)==original,
      "Verified example session replays engineering values without replacing the active profile");
  }
  if(imported && documentDialog && expectedSignals!=18 && expectedSignals!=19) {
    backend.replaySource=false;
    backend.applyProfile(importedMappings);
    const auto before=aero::serializeProfile(backend.profile);
    documentDialog->findChild<QLineEdit *>("actionQuestion")->setText("Set TEMP scale to 0.02");documentDialog->findChild<QPushButton *>("previewAction")->click();
    const auto previewed=wait([&]{return documentDialog->findChild<QPushButton *>("applyAction")->isEnabled();},15000);
    check(previewed && before==aero::serializeProfile(backend.profile),"Natural-language clarification previews without mutating approved mappings");
    if(previewed){
      documentDialog->grab().save(directory+"/reviewed-action.png");
      documentDialog->findChild<QPushButton *>("applyAction")->click();
      check(backend.profile[0].scale==.02 && !backend.profile[0].operatorNote.empty(),"Reviewed clarification reaches the C++ conversion profile with operator provenance");
      documentDialog->findChild<QComboBox *>("answerSource")->setCurrentIndex(1);
      documentDialog->findChild<QLineEdit *>("question")->setText("TEMP scale");
      documentDialog->findChild<QPushButton *>("askButton")->click();
      check(wait([&]{
        const auto answer=documentDialog->findChild<QTextBrowser *>("answer")->toPlainText();
        return answer.contains("Published mappings (Engineering)") && answer.contains("scale 0.02") &&
          answer.contains("ICD document") && answer.contains("0.01");
      },15000),"Published chat distinguishes the edited engineering rule from the extracted PDF rule");
    }
  }
  if(documentDialog) documentDialog->close();
  QFile output(directory+"/document-workflow.txt");
  if(output.open(QIODevice::WriteOnly)) output.write(results.join('\n').toUtf8());else passed=false;
  return passed;
}
