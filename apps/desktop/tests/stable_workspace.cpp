#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "services/csv_conversion.hpp"

bool Window::stableWorkspaceTest(const QString &directory) {
  QDir().mkpath(directory);QFile file(directory+"/stable-workspace.txt");if(!file.open(QIODevice::WriteOnly|QIODevice::Text))return false;QTextStream report(&file);bool good=true;
  const auto check=[&](bool ok,const QString &text){report<<(ok?"PASS: ":"FAIL: ")<<text<<'\n';report.flush();good&=ok;};
  const auto settle=[] {QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();};
  unlocked=true;backend.captureConfig.hardware=false;workspaceSubsystem="Subsystem-1";workspaceScopeKey="live";settings.setValue("workspace/live/subsystem",workspaceSubsystem);analysisChannel=1;analysisBus=0;
  aero::Signal s;s.id="Subsystem-1 / 04102 / ID_1";s.sourceSignalId="ID_1";s.subsystem="Subsystem-1";s.messageId="04102";s.rt=4;s.sa=2;s.transmit=true;s.width=32;s.numericType="float32";s.scale=1;s.offset=0;s.unit="Rad";s.channel=1;s.expectedWordCount=32;s.icdSetId="fixture";s.icdRevision="revision";
  backend.profile={s};preferredSignalIndex=0;setSidebarCollapsed(false);resize(1366,768);show();navigate(6);settle();
  setProperty("icdArchivePending",true);check(!publishProfile({}).isEmpty()&&backend.profile.size()==1&&backend.profile.front().id==s.id,"A pending archive prevents another publication from replacing its rollback context");setProperty("icdArchivePending",false);
  check(!nav[6]->isVisible()&&!nav[7]->isVisible()&&nav[12]->text()=="ICD && Analysis"&&nav[12]->isChecked(),"One sidebar destination represents all workspace tabs");
  check(!sidebarCollapsed&&sidebar->width()==220,"Entering the workspace preserves the operator's expanded navigation");
  auto raw=content->findChild<QPlainTextEdit *>("rawWords");auto preview=content->findChild<QPushButton *>("previewButton");auto result=content->findChild<QPlainTextEdit *>("previewResult");
  check(raw&&preview&&result&&plots.empty(),"Engineering shows mapping setup and preview without a live trend or numeric tiles");
  if(raw&&preview&&result){raw->setPlainText("3F80 0000");preview->click();check(result->property("decodedValue").toDouble()==1.,"MSW-first IEEE 754 raw words decode through the native service");const auto bounds=content->geometry(),table=qualityTable->geometry();for(int i=0;i<120;++i){raw->setPlainText(i%2?"7F7F FFFF":"BF80 0000");preview->click();refreshDataViews();settle();}check(content->geometry()==bounds&&qualityTable->geometry()==table,"Extreme changing numeric results do not resize the layout");}
  aero::Signal restored;check(desktop::signalFromJson(desktop::signalToJson(s),restored).isEmpty()&&restored.icdSetId==s.icdSetId&&restored.icdRevision==s.icdRevision,"ICD ownership metadata survives profile round-trip");
  grab().save(directory+"/engineering.png");navigate(7);settle();
  check(!plots.empty()&&plots.front()->isVisible()&&!plots[1]->isVisible(),"Analysis defaults to one large selected trace");
  if(!plots.empty()){
    const auto graph=plots.front()->findChild<QWidget *>("analysisCanvas");auto body=content->findChild<QWidget *>("PlotsPage");
    const double ratio=graph&&body?double(graph->height())/body->height():0.;report<<"Chart / analysis body height ratio: "<<ratio<<'\n';
    check(graph&&ratio>=.65,"Plot receives at least 65 percent of analysis body height at 1366 x 768");
    const auto bounds=plots.front()->geometry();for(int i=0;i<80;++i){plots.front()->setStatus(QString(300,'x')+QString::number(i));plots.front()->setSeries({{0,0},{.5,3.402823466e38},{.75,NAN},{.25,-3.402823466e38},{1,1}});settle();}
    check(plots.front()->geometry()==bounds,"Long status strings and float32 extremes preserve chart geometry");check(plots.front()->property("timeDiscontinuities").toInt()==1,"Backward timestamps create visible segment boundaries");
    auto stats=content->findChild<QLabel *>("rangeStatistics");plots.front()->showStatistics(stats);check(stats&&stats->text().contains("4 samples"),"Range statistics exclude invalid/gap samples");
#ifdef AEROTRACE_QWT
    for(auto action:content->findChildren<QAction *>())if(action->text()=="Zoom")action->trigger();auto zoomer=plots.front()->findChild<QwtPlotZoomer *>();
    if(zoomer){check(zoomer->zoomBase().top()<0&&zoomer->zoomBase().bottom()>3e38,"Zoom starts from the current engineering axes, including negative values");zoomer->zoom(QRectF(.1,-1e38,.4,2e38));check(zoomer->zoomRectIndex()>0&&!plots.front()->followsLive(),"Native rectangle zoom holds the operator's range");plots.front()->zoomBack();check(zoomer->zoomRectIndex()==0,"Zoom back restores the prior view");}else check(false,"Native zoom controller is available");
    plots.front()->setInteraction(1);plots.front()->setInteraction(0);
#endif
    plots.front()->setAxes(0,.5);check(!plots.front()->followsLive(),"Manual axes disable live follow");plots.front()->resetView();check(plots.front()->followsLive(),"Follow restores automatic axes");
    auto focus=content->findChild<QAction *>("focusPlot");if(focus){focus->setChecked(true);settle();check(!sidebar->isVisible(),"Focus plot hides surrounding navigation");focus->setChecked(false);settle();check(sidebar->isVisible()&&!sidebarCollapsed,"Leaving focus restores the operator's navigation state");}else check(false,"Focus action exists");
  }
  if(auto compare=content->findChild<QPushButton *>("restorePlotsButton")){compare->setChecked(true);settle();check(plots.size()==4&&std::all_of(plots.begin(),plots.end(),[](auto plot){return plot->isVisible();}),"Comparison exposes four separate signal panes");compare->setChecked(false);settle();}
  if(plots.size()>1){plots[0]->onPick(QPointF(0,0));plots[1]->setSeries({{0,42},{1,43}});plots[1]->onPick(QPointF(0,42));check(cursorAValue&&cursorAValue->y()==42&&cursorDeltaLabel->text()=="Δt / ΔY","Switching comparison traces starts a new cursor pair");const auto originalChannel=plots[1]->property("sourceChannel");plots[1]->setProperty("sourceChannel",2);plots[1]->onPick(QPointF(1,43));check(cursorAValue&&cursorAValue->y()==43&&cursorDeltaLabel->text()=="Δt / ΔY","Switching channels never subtracts unrelated device clocks");plots[1]->setProperty("sourceChannel",originalChannel);cursorAValue.reset();}
  grab().save(directory+"/analysis.png");const auto windowSize=size();documentCenter();settle();check(documentDialog&&documentDialog->isWindow()&&size()==windowSize&&!documentDock,"Assistant is a separate window without resizing the main application");
  if(documentDialog){auto panel=documentDialog->findChild<QWidget *>("conversationSidebar");auto composer=documentDialog->findChild<QPlainTextEdit *>("chatComposer");check(panel&&composer&&composer->mapTo(documentDialog,QPoint()).x()>=panel->geometry().right(),"Conversation content occupies its own column without overlapping history");check(documentDialog->findChild<QListWidget *>("conversationList")&&documentDialog->findChild<QPlainTextEdit *>("chatComposer"),"Assistant exposes conversation history and a multiline composer");documentDialog->grab().save(directory+"/assistant.png");documentDialog->close();settle();}
  navigate(12);settle();check(nav[12]->isChecked()&&!sidebarCollapsed,"Mapping tab retains shared navigation selection and expansion");
  // Exercise the real streaming CSV workflow on a file larger than the old
  // 32 MiB batch limit, while measuring the GUI's event-loop heartbeat.
  QTemporaryDir temporary;
  const auto source=temporary.filePath("large.csv"),converted=temporary.filePath("decimal.csv"),cancelled=temporary.filePath("cancelled.csv");
  aero::Record record;record.source=1;record.validity=5;record.messageType=1;record.channel=1;record.commandWord=0x2440;record.statusWord=0x2000;record.vendorStatus=0x8100;record.words.assign(32,0xffff);record.words[0]=0x3f80;record.words[1]=0;
  {QFile fixture(source);if(fixture.open(QIODevice::WriteOnly)){fixture.write(QByteArray::fromStdString(aero::sessionCsvHeader()));const auto block=QByteArray::fromStdString(aero::sessionCsvRecord(record)).repeated(1000);for(int i=0;i<140;++i)fixture.write(block);}}
  check(QFileInfo(source).size()>32*1024*1024,"Large CSV fixture exceeds the former batch-size limit");
  const auto hash=[](const QString &path){QFile f(path);QCryptographicHash h(QCryptographicHash::Sha256);if(!f.open(QIODevice::ReadOnly)||!h.addData(&f))return QByteArray();return h.result();};const auto originalHash=hash(source);
  const bool nativeDialogs=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
  csvTools();auto csvDialog=findChild<QDialog *>("savedCsvTools");settle();
  const auto wait=[](std::function<bool()> predicate,int timeout=60000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<timeout){QApplication::processEvents();QThread::msleep(1);}return predicate();};
  if(csvDialog){
    auto path=csvDialog->findChild<QLineEdit *>("savedCsvPath");auto preview=csvDialog->findChild<QPushButton *>("previewCsv");auto save=csvDialog->findChild<QPushButton *>("convertCsv");auto status=csvDialog->findChild<QLabel *>("csvConversionStatus");auto table=csvDialog->findChild<QTableWidget *>("csvPreview");auto radix=csvDialog->findChild<QComboBox *>("outputCsvBase");path->setText(source);radix->setCurrentIndex(radix->findData(10));preview->click();
    check(wait([&]{return preview->isEnabled();})&&table->rowCount()==100&&table->item(0,8)->text().startsWith("16256  0"),"Saved CSV preview pages 100 rows and preserves raw bits in decimal");
    const auto choose=[csvDialog](const QString &destination){QTimer::singleShot(80,csvDialog,[csvDialog,destination]{for(auto chooser:csvDialog->findChildren<QFileDialog *>()){chooser->selectFile(destination);QMetaObject::invokeMethod(chooser,"accept",Qt::QueuedConnection);}});};
    QTimer heartbeat;heartbeat.setInterval(5);QElapsedTimer clock;clock.start();qint64 previous=0,worst=0;unsigned beats=0;connect(&heartbeat,&QTimer::timeout,csvDialog,[&]{const auto now=clock.elapsed();worst=std::max(worst,now-previous);previous=now;++beats;});
    choose(converted);save->click();report<<"File chooser interaction ms: "<<clock.restart()<<'\n';heartbeat.start();const bool complete=wait([&]{return save->isEnabled();});heartbeat.stop();
    report<<"Large CSV bytes: "<<QFileInfo(source).size()<<"; heartbeat samples: "<<beats<<"; worst heartbeat gap ms: "<<worst<<'\n';
    report<<"Conversion elapsed ms: "<<clock.elapsed()<<"; status: "<<status->text()<<'\n';
    check(complete&&status->text().startsWith("Exported 140000")&&QFileInfo::exists(converted),"Full large-file conversion publishes all rows through the background job");
    check(beats>5&&worst<1000,"CSV conversion leaves the GUI event loop responsive");
    choose(cancelled);save->click();QTimer::singleShot(20,csvDialog,[csvDialog]{csvDialog->findChild<QPushButton *>("cancelCsv")->click();});
    check(wait([&]{return save->isEnabled();})&&status->text().startsWith("Cancelled")&&!QFileInfo::exists(cancelled),"Cancellation leaves no partially published CSV");
    auto mode=csvDialog->findChild<QComboBox *>("csvConversionMode");mode->setCurrentIndex(1);csvDialog->findChild<QCheckBox *>("confirmCsvRules")->setChecked(true);preview->click();
    check(wait([&]{return preview->isEnabled();})&&table->columnCount()==8&&table->rowCount()==100&&table->item(0,5)->text()=="1", "CSV engineering preview agrees with the same native float conversion");
    check(hash(source)==originalHash,"Raw source CSV remains byte-for-byte unchanged");csvDialog->grab().save(directory+"/csv-tools.png");
    mode->setCurrentIndex(0);preview->click();check(wait([&]{return preview->isEnabled();}),"Raw CSV preview is ready for plotting");
    QTimer chooseTrace;chooseTrace.setInterval(20);
    connect(&chooseTrace,&QTimer::timeout,this,[&]{if(auto choose=findChild<QDialog *>("recordedCsvSelection")){choose->findChild<QComboBox *>("recordedCsvChannel")->setCurrentIndex(1);if(auto buttons=choose->findChild<QDialogButtonBox *>())buttons->button(QDialogButtonBox::Ok)->click();chooseTrace.stop();}});
    chooseTrace.start();csvDialog->findChild<QPushButton *>("plotCsvSelection")->click();chooseTrace.stop();settle();
    auto csvStatus=content->findChild<QLabel *>("recordedCsvStatus");
    check(recordedCsv.has_value()&&page==7&&csvStatus&&!backend.acquisition.active(),"Plot selected opens Recorded CSV Analysis with capture stopped");
    check(csvStatus&&wait([&]{return !csvStatus->text().startsWith("Scanning");}),"Recorded CSV scan completes in the background");
    auto csvStatistics=content->findChild<QLabel *>("recordedCsvStatistics");
    check(csvStatistics&&csvStatistics->text().contains("140000 samples"),"Recorded CSV statistics include every matching row rather than only plotted points");
    if(!plots.empty()){
      auto canvas=plots.front()->findChild<QWidget *>("analysisCanvas");auto body=content->findChild<QWidget *>("PlotsPage");
      report<<"Recorded CSV chart / analysis body height ratio: "<<(canvas&&body?double(canvas->height())/body->height():0.)<<'\n';
      check(canvas&&body&&double(canvas->height())/body->height()>=.65,"Recorded CSV keeps a large analysis canvas");
      plots.front()->onPick(QPointF(0,16256));auto cursor=content->findChild<QLabel *>("cursorA");
      check(cursor&&cursor->text().contains("Row 1"),"Recorded CSV cursor identifies its original source row");
    }
    check(hash(source)==originalHash,"Recorded CSV analysis preserves the source file");grab().save(directory+"/recorded-csv-analysis.png");
    recordedCsv.reset();navigate(7);settle();
  }else check(false,"Saved CSV tools open successfully");
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,nativeDialogs);
  return good;
}
