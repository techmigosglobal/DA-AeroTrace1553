#include "main_window.hpp"
namespace {
class RtTraffic final : public aero::CaptureSource {
  std::uint64_t id{};
public:
  std::string open(const aero::CaptureConfig &) override{return {};}
  std::string poll(aero::SourceBatch &batch,std::stop_token) override {
    for(int repeat=0;repeat<20;++repeat)for(unsigned ch=1;ch<=2;++ch)for(unsigned bus=0;bus<2;++bus)for(unsigned rt:{2u,3u}){
      auto r=aero::synthetic(0);r.id=++id;r.channel=ch;r.bus=bus;r.commandWord=(rt<<11)|(2<<5)|8;r.statusWord=rt<<11;r.timestampNs=id*2000;r.words[0]=100*ch+10*bus+rt;batch.records.push_back(std::move(r));}
    std::this_thread::sleep_for(std::chrono::milliseconds(15));return {};
  }
  void close() override{}
};
}
bool Window::rtMonitorWorkflowTest(const QString &directory) {
  QDir().mkpath(directory);QTemporaryDir capture;QStringList results;bool passed=true;
  QMap<QString,QVariant> previous;for(const auto &key:settings.allKeys())previous[key]=settings.value(key);
  auto check=[&](bool ok,const QString &name){passed&=ok;results<<(ok?"PASS: ":"FAIL: ")+name;};
  auto settle=[](int milliseconds){QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();};
  settings.setValue("monitorRowMode","statistics");settings.setValue("monitorLayout","windows");settings.remove("monitorWindow0");settings.remove("monitorWindow1");settings.setValue("activeMonitorChannel",0);
  for(int c=0;c<2;++c){settings.setValue(QString("pane%1Bus").arg(c),"");settings.setValue(QString("pane%1Rt").arg(c),-1);settings.setValue(QString("pane%1Search").arg(c),"");}
  unlocked=true;backend.acquisition.stop();backend.archive.clear();model.records.clear();model.refresh();
  aero::CaptureConfig config;config.hardware=false;config.enabled={true,true,true,true};config.displayCapacity=2;
  backend.captureConfig=config;
  currentFile=capture.path()+"/grouped.atd";
  check(backend.acquisition.startConfigured(std::filesystem::path(currentFile.toStdWString()),config,std::make_unique<RtTraffic>()),"start bounded-queue RT source");
  resize(1700,1000);navigate(4);settle(450);
  check(rtViews[0]&&rtViews[1]&&rtViews[0]->isVisible()&&rtViews[1]->isVisible(),"External and Internal grouped views are side by side");
  check(rtModels[0].rowCount()==4&&rtModels[1].rowCount()==4&&rtModels[0].columnCount()==10,"One stable row per active RT/bus per channel, ten statistics columns");
  check(rtModels[0].horizontalHeaderItem(9)->text()=="Total"&&rtModels[0].item(0,4)->text()=="—","Unsupported error categories remain unavailable rather than false zero");
  const QPersistentModelIndex selected(rtModels[0].index(2,0));rtViews[0]->setCurrentIndex(selected);
  const auto frame=backend.acquisition.rtFrame();const auto before=frame?frame->channels[0].at(3).buses[0].count:0;
  settle(300);const auto latest=backend.acquisition.rtFrame();
  check(selected.isValid()&&selected==rtViews[0]->currentIndex()&&rtModels[0].rowCount()==4,"Updates preserve RT/bus row identity and selection");
  check(latest&&latest->channels[0].at(3).buses[0].count>before&&latest->channels[0].at(3).buses[0].latest.words[0]==103&&latest->channels[0].at(3).buses[1].latest.words[0]==113&&latest->channels[1].at(3).buses[0].latest.words[0]==203,"Bus A/B values and identical RTs on different channels remain independent");
  check(backend.acquisition.metrics(false).displayDrops>0&&latest&&latest->channels[0].at(3).buses[0].count>model.records.size()/8,"RT aggregate survives raw display queue thinning");
  auto bus=content->findChild<QComboBox *>("pane0BusFilter");bus->setCurrentIndex(1);settle(50);check(selectedMonitorRecord()&&selectedMonitorRecord()->bus==0&&selectedMonitorRecord()->channel==1,"Grouped selection resolves the selected bus's actual latest record");
  bus->setCurrentIndex(0);settle(50);grab().save(directory+"/rt-monitor.png");grab().save(directory+"/rt-statistics.png");
  auto pause=content->findChild<QPushButton *>("pauseRtDisplay");pause->click();const auto frozen=rtModels[0].item(2,9)->text();const auto frozenCaption=paneCounts[0]->text();const auto recorded=backend.acquisition.metrics(false).recorded;settle(180);
  check(rtModels[0].item(2,9)->text()==frozen&&paneCounts[0]->text()==frozenCaption&&backend.acquisition.metrics(false).recorded>recorded,"Pause freezes statistics and channel totals while raw recording continues");pause->click();settle(60);
  auto grouping=content->findChild<QComboBox *>("monitorRowMode");grouping->setCurrentIndex(1);settle(50);check(rtModels[0].rowCount()==2&&rtModels[0].columnCount()==6,"Message routes remain available as an alternate mode");
  rtViews[0]->expandAll();grab().save(directory+"/rt-routes.png");grouping->setCurrentIndex(0);settle(50);
  const auto record=selectedMonitorRecord();const auto id=record?record->id:0;inspectSelection();check(page==5&&id&&backend.selected.id==id,"Inspect opens the exact grouped message");
  backend.acquisition.stop();navigate(4);auto mode=content->findChild<QComboBox *>("monitorRowMode");mode->setCurrentIndex(2);check(paneViews[0]->isVisible()&&!rtViews[0]->isVisible(),"Chronological raw view remains available");
  loadSession(currentFile);for(int i=0;i<200&&replayLoadPending;++i)settle(25);
  check(replayRtFrame&&backend.recordingIndex&&replayRtFrame->totalCommands[0]+replayRtFrame->totalCommands[1]==backend.recordingIndex->size(),"Replay statistics scan the entire indexed recording");
  if(auto action=content->findChild<QPushButton *>("showReplayRtStatistics")){action->click();settle(60);check(viewingReplayStatistics&&paneCounts[0]->text().contains("Recorded session · full file")&&displayedRtFrame==replayRtFrame,"Recorded statistics have explicit full-file scope and independent channel totals");grab().save(directory+"/rt-statistics-recorded.png");}else check(false,"Replay statistics action exists");
  documentCenter();settle(50);QPointer<QDialog> closing=documentDialog;if(closing)closing->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);settle(50);
  check(closing.isNull(),"Closing an active document dialog cancels without callbacks into destroyed widgets");
  settings.clear();for(auto i=previous.begin();i!=previous.end();++i)settings.setValue(i.key(),i.value());settings.sync();
  QFile report(directory+"/rt-monitor.txt");if(!report.open(QIODevice::WriteOnly))return false;report.write(results.join('\n').toUtf8());return passed;
}
