#include "main_window.hpp"
#include <QCryptographicHash>
#include "ui_sessions_page.h"
#include "ui_replay_page.h"
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#include "services/runtime_paths.hpp"
#include "services/operator_style.hpp"
#include "sequence_analysis.hpp"

QStringList Window::recordingFiles(){
    QStringList roots{sessionDir};roots+=settings.value("legacySessionRoots").toStringList();roots<<desktop::dataDirectory()+"/Sessions";
    QStringList result;for(const auto &root:roots){QDirIterator files(root,{"*.atd"},QDir::Files,QDirIterator::Subdirectories);while(files.hasNext()){auto path=files.next();if(!result.contains(path))result<<path;}}
    std::sort(result.begin(),result.end(),[](const auto &a,const auto &b){return QFileInfo(a).lastModified()>QFileInfo(b).lastModified();});
    return result;
  }

void Window::sessions() {
  auto form=showForm<Ui::SessionsPage>();
  form.sessionActions->hide();
  form.sessionDetails->hide();
  auto convert=new QPushButton("Open CSV Viewer",content);desktop::actionIcon(convert,"file");
  form.actionBarLayout->addWidget(convert);
  form.actionBarSpacer->changeSize(0,0,QSizePolicy::Ignored,QSizePolicy::Minimum);
  for(auto btn:{form.newRecordingButton,form.openSelectedButton,form.importRecordingButton,form.exportAnalysisButton,convert})
    form.actionBarLayout->setStretchFactor(btn,1);
  form.selectedSessionBoxLayout->setStretch(1,1);
  form.sessionStatusBannerLayout->setStretch(1,1);
  desktop::actionIcon(form.newRecordingButton,"add-file",true);
  desktop::actionIcon(form.openSelectedButton,"folder");
  desktop::actionIcon(form.importRecordingButton,"download");
  desktop::actionIcon(form.exportAnalysisButton,"download");
  desktop::labelIcon(form.savedSessionsIcon,"layers",20);
  desktop::labelIcon(form.replayCardIcon,"play",20);
  desktop::labelIcon(form.selectedSessionIcon,"file",16);
  {
    auto infoPm=QIcon(":/icons/info.svg").pixmap(QSize(18,18),devicePixelRatioF());
    QPainter p(&infoPm);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(infoPm.rect(),QColor("#16804a"));p.end();
    form.sessionInfoIcon->setPixmap(infoPm);
  }
  form.savedSessionsTitle->setStyleSheet("font-size:17px;font-weight:700;color:#142d3a;");
  form.replayCardTitle->setStyleSheet("font-size:17px;font-weight:700;color:#142d3a;");
  form.selectedSessionLabel->setStyleSheet("font-size:13px;font-weight:600;color:#172b3a;");
  form.replayOptionsLabel->setStyleSheet("font-size:13px;font-weight:600;color:#172b3a;");
  form.selectedSessionBox->setStyleSheet("background:#eef3f7;border:1px solid #d4e0e9;border-radius:8px;");
  form.selectedSessionName->setStyleSheet("font-size:13px;font-weight:500;color:#172b3a;");
  form.sessionStatusBanner->setStyleSheet("background:#eef6f1;border:1px solid #cde2d5;border-radius:8px;");
  form.sessionCount->setStyleSheet("font-size:13px;color:#1b432e;");
  form.sessionSearch->addAction(desktop::operatorIcon("search"),QLineEdit::LeadingPosition);
  form.replayBusCombo->addItems({"All buses","Bus A","Bus B"});
  form.replaySpeedCombo->addItems({"0.25×","Real-time (1×)","2×","5×"});
  form.replaySpeedCombo->setCurrentIndex(1);
  desktop::actionIcon(form.startReplayButton,"play",true);
  connect(convert,&QPushButton::clicked,this,[this]{const auto row=sessionTable?sessionTable->currentRow():-1;csvTools(sessionPaths.value(row));});
  connect(form.startReplayButton,&QPushButton::clicked,this,[this]{const auto row=sessionTable?sessionTable->currentRow():-1;if(row>=0&&row<sessionPaths.size())loadSession(sessionPaths[row]);else toast("Select a recording first.");});
  connect(form.newRecordingButton,&QPushButton::clicked,this,[this] { startCapture(); });
  connect(form.importRecordingButton,&QPushButton::clicked,this,[this] { openSession(); });
  connect(form.allSessionsButton,&QPushButton::clicked,this,[this] { navigate(10); });
  connect(form.refreshButton,&QPushButton::clicked,this,[this] { navigate(10); });
  connect(form.renameButton,&QPushButton::clicked,this,[this] { renameSession(); });
  connect(form.exportAnalysisButton,&QPushButton::clicked,this,[this] {
    if(connecting||savingSession||backend.acquisition.active()){toast("Stop capture before exporting analysis");return;}
    const auto row=sessionTable->currentRow();if(row<0||row>=sessionPaths.size()){toast("Select a recording first");return;}
    const auto input=sessionPaths[row];const auto output=QFileDialog::getSaveFileName(this,"Export passive analysis",input+".analysis.jsonl","JSON Lines (*.jsonl)");
    if(output.isEmpty())return;
    if(QFileInfo(input).absoluteFilePath().compare(QFileInfo(output).absoluteFilePath(),Qt::CaseInsensitive)==0){toast("Choose a different output file");return;}
    auto watcher=new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher]{const auto result=watcher->result();toast(result.isEmpty()?"Passive analysis exported":result);watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([input,output] {
      QSaveFile file(output);if(!file.open(QIODevice::WriteOnly))return QString("Cannot open analysis export: ")+file.errorString();
      auto write=[&](const QJsonObject &object){const auto line=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';if(file.write(line)!=line.size())throw std::runtime_error("Analysis export write failed");};
      try {
        write({{"schema","aerotrace.passive-analysis.v1"},{"profile","1553b-baseline-review"},{"recording",QFileInfo(input).fileName()},{"limitations","Passive observations only; capture history, loss, timing and terminal conformance are not established."}});
        aero::SequenceAnalysis sequence;
        const auto result=aero::scanRecording(std::filesystem::path(input.toStdWString()),[&](const aero::Record &record,std::uint64_t){
          const auto p=aero::interpret(record);QJsonArray observations;
          for(const auto &o:sequence.consume(record))observations.append(QJsonObject{{"kind",o.kind},{"record_id",QString::number(o.recordId)},{"prior_record_id",o.priorRecordId?QString::number(o.priorRecordId):QString()},{"evidence",o.evidence},{"conclusion","insufficient evidence for conformance"}});
          write({{"record_id",QString::number(record.id)},{"channel",int(record.channel)},{"bus",record.bus?"B":"A"},{"source",record.source==1?"DDC_MTI":record.source==2?"DDC_MT":"SIMULATED_OR_LEGACY"},{"structure",!p.supported?"unknown":p.valid?"checks passed":"review"},{"terminal_busy",p.terminalBusy},{"terminal_fault",p.terminalFault},{"terminal_message_error",p.terminalMessageError},{"payload_eligible",p.payloadEligible},{"reason",QString::fromUtf8(p.reason.data(),qsizetype(p.reason.size()))},{"vendor_status",int(record.vendorStatus)},{"timing","not measured"},{"observations",observations}});
          return true;
        });
        if(!result.error.empty())return QString::fromStdString(result.error);
        write({{"record_count",QString::number(result.count)},{"recovered",result.recovered},{"capture_completeness","not established by recording scan"}});
        if(!file.commit())return QString("Analysis export commit failed: ")+file.errorString();
      } catch(const std::exception &e){return QString::fromUtf8(e.what());}
      return QString();
    }));
  });
  sessionTable=form.sessionTable; sessionSearch=form.sessionSearch; sessionPaths=recordingFiles();
  QList<QStringList> rows;
  for(const auto &path:sessionPaths) rows << QStringList{desktop::sessionDisplayName(path),QFileInfo(path).lastModified().toString("dd MMM, HH:mm"),fileSize(path),path==currentFile && backend.acquisition.active() ? "Recording" : "Local file"};
  populate(sessionTable,rows);
  for(int i=0;i<sessionTable->rowCount();++i) {
    if(auto *item=sessionTable->item(i,0)) item->setIcon(desktop::operatorIcon("play"));
  }
  const auto updateSelectedSession=[this,form,total=rows.size()]{
    const int row=sessionTable?sessionTable->currentRow():-1;
    if(row>=0 && row<sessionPaths.size()) {
      const auto name=desktop::sessionDisplayName(sessionPaths[row]);
      const auto sz=fileSize(sessionPaths[row]);
      const auto loc=sessionPaths[row]==currentFile && backend.acquisition.active() ? QString("Recording") : QString("Local file");
      form.selectedSessionName->setText(name);
      form.sessionCount->setText(QString("%1 of %2 sessions selected · %3 · %4").arg(row+1).arg(total).arg(sz,loc));
    } else {
      form.selectedSessionName->setText("No session selected");
      form.sessionCount->setText(QString("%1 local sessions · No cloud storage").arg(total));
    }
  };
  connect(sessionTable,&QTableWidget::currentCellChanged,this,[updateSelectedSession](int,int,int,int){updateSelectedSession();});
  if(sessionTable->rowCount()>0) sessionTable->selectRow(0);
  updateSelectedSession();
  connect(sessionSearch,&QLineEdit::textChanged,this,[this](const QString &text) {
    for(int i=0;i<sessionTable->rowCount();++i) sessionTable->setRowHidden(i,!sessionTable->item(i,0)->text().contains(text,Qt::CaseInsensitive));
  });
  connect(sessionTable,&QTableWidget::cellDoubleClicked,this,[this](int index,int) {
    if(index>=0 && index<sessionPaths.size()) loadSession(sessionPaths[index]);
  });
  connect(form.openSelectedButton,&QPushButton::clicked,this,[this] {
    const auto index=sessionTable->currentRow();
    if(index>=0 && index<sessionPaths.size()) loadSession(sessionPaths[index]);
  });
  form.sessionRoot->setText(sessionDir);
  form.availableCapacity->setText(QString::number(QStorageInfo(sessionDir).bytesAvailable()/1073741824.,'f',1)+" GB");
}


void Window::renameSession(){
    int r = sessionTable->currentRow();
    if (r < 0 || r >= sessionPaths.size())
      return;
    QString path = sessionPaths[r];
    if (savingSession || connecting || backend.acquisition.active()) {
      toast("Stop the active capture before renaming.");
      return;
    }
    auto name =
        QInputDialog::getText(this, "Rename session", "Name", QLineEdit::Normal,
                              QFileInfo(path).baseName());
    if (name.isEmpty() || name.contains('/') || name.contains('\\') ||
        name.contains(".."))
      return;
    auto dest = QFileInfo(path).dir().filePath(name + ".atd");
    if(currentReplay==path){backend.recordingIndex.reset();backend.replayRecords.clear();backend.replayPlaying=false;replayRtFrame.reset();viewingReplayStatistics=false;}
    if(auto error=desktop::renameRecording(path,dest);!error.isEmpty())toast(error);
    else {if(currentFile==path)currentFile=dest;if(currentReplay==path)currentReplay=dest;}
    navigate(10);
  }

void Window::openSession(){
    auto p =
        QFileDialog::getOpenFileName(this, "Open recording", sessionDir,
                                     "AeroTrace development recording (*.atd)");
    if (!p.isEmpty())
      loadSession(p);
  }

void Window::loadSession(QString p){
  recordedCsv.reset();
    applicationEvent("replay_open_request",p);
    if (connecting || savingSession || backend.acquisition.active()) {
      toast("Stop capture before opening a recording.");
      return;
    }
    const auto loadGeneration=++*replayGeneration;const auto loadRequest=++replayLoadRequest;replayLoadPending=true;replaySeekPending=false;
    struct Loaded {std::shared_ptr<aero::RecordingIndex> first; aero::ScanResult second;std::shared_ptr<std::vector<aero::RtActivity>> checkpoints;std::shared_ptr<const aero::RtMonitorFrame> statistics;};
    auto watcher=new QFutureWatcher<Loaded>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher,p,loadGeneration,loadRequest]{
      auto result=watcher->result();watcher->deleteLater();
      if(loadRequest!=replayLoadRequest)return;replayLoadPending=false;
      if(loadGeneration!=replayGeneration->load())return;
      applicationEvent("replay_open_result",p,{{"records",QString::number(result.second.count)},{"recovered",result.second.recovered},{"error",QString::fromStdString(result.second.error)}});
      if(!result.second.error.empty()){toast(QString::fromStdString(result.second.error));return;}
      if(backend.acquisition.active()||connecting||savingSession){toast("Capture state changed; reopen the recording when stopped.");return;}
      replayCheckpoints=result.checkpoints;replayActivity={};replayRtFrame=result.statistics;viewingReplayStatistics=false;++*replayGeneration;
      try{backend.recordingIndex=std::move(result.first);backend.replaySource=true;backend.replayPlaying=false;backend.replayIndex=0;backend.replayWindowStart=0;backend.replayRecords.clear();backend.selected={};}catch(const std::exception &e){backend.replaySource=false;toast(QString::fromUtf8(e.what()));return;}
      currentReplay=p;
      backend.replayProfile.clear();
      backend.replayExample=desktop::verifiedOfflineFixture(p);
      QFile profileFile(p+".profile.json");
      replayProfileDescription="Raw-only session";
      QString profileError;
      if(profileFile.exists()) {
        if(!profileFile.open(QIODevice::ReadOnly) || profileFile.size()>4*1024*1024)
          profileError="Cannot read profile or file exceeds 4 MB";
        else {
          const auto bytes=profileFile.readAll();
          QJsonParseError syntax;
          const auto object=QJsonDocument::fromJson(bytes,&syntax).object();
          std::vector<aero::Signal> mappings;
          profileError=syntax.error==QJsonParseError::NoError?desktop::profileFromJson(object,mappings):syntax.errorString();
          if(profileError.isEmpty() && !mappings.empty()) {
            backend.replayProfile=std::move(mappings);
            const auto revision=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().left(12));
            replayProfileDescription="Latest saved profile · "+revision;
            QFile journal(p+".icd.jsonl");
            if(journal.open(QIODevice::ReadOnly) && !journal.readLine().isEmpty() && !journal.readLine().isEmpty())
              replayProfileDescription+=" · multiple revisions; latest applied throughout";
          }
        }
      }
      eventText=result.second.recovered?"Recovered recording: incomplete tail discarded":"Recording CRC validation passed; device clocks remain independent";
      if(!profileError.isEmpty()) {
        eventText+=" · "+QFileInfo(profileFile).fileName()+": "+profileError;
        qWarning().noquote()<<"Recording ICD profile rejected:"<<profileFile.fileName()<<profileError;
      }
      navigate(11);toast(eventText);
    });
    watcher->setFuture(QtConcurrent::run([p,loadGeneration,cancel=replayGeneration]{auto index=std::make_shared<aero::RecordingIndex>();auto checkpoints=std::make_shared<std::vector<aero::RtActivity>>(1);auto statistics=std::make_shared<aero::RtMonitorFrame>();statistics->session=p.toStdString()+"/full-file";aero::RtActivity activity;std::uint64_t count=0;
      auto status=index->open(std::filesystem::path(p.toStdWString()),[&](const auto &r){if(cancel->load()!=loadGeneration)throw std::runtime_error("Recording load cancelled");activity.observe(r);statistics->consume(r);if(++count%4096==0)checkpoints->push_back(activity);});return Loaded{index,status,checkpoints,statistics};}));
    toast("Validating recording and building replay index…");
  }

void Window::replay() {
  backend.replaySource=bool(backend.recordingIndex)||!backend.replayRecords.empty();
  auto form=showForm<Ui::ReplayPage>();
  auto statistics=new QPushButton("RT statistics · full recorded session");statistics->setObjectName("showReplayRtStatistics");statistics->setEnabled(bool(replayRtFrame));pageLayout->insertWidget(0,statistics,0,Qt::AlignLeft);
  connect(statistics,&QPushButton::clicked,this,[this]{if(!replayRtFrame)return;backend.replayPlaying=false;viewingReplayStatistics=true;rtDisplayPaused=false;settings.setValue("monitorRowMode","statistics");settings.setValue("monitorLayout","windows");navigate(4);});
  connect(form.openReplayButton,&QPushButton::clicked,this,[this] { openSession(); });
  if(form.inspectReplayButton) form.inspectReplayButton->hide();
  form.speed->setCurrentIndex(backend.replaySpeed==.25 ? 0 : backend.replaySpeed==1 ? 1 : backend.replaySpeed==2 ? 2 : 3);
  connect(form.speed,&QComboBox::currentIndexChanged,this,[this](int index) { backend.replaySpeed=QList<double>{.25,1,2,5}[index]; });
  connect(form.rewindButton,&QPushButton::clicked,this,[this] { seekReplay(0); });
  connect(form.playButton,&QPushButton::clicked,this,[this] {
    if(!backend.replayCount()) { toast("Open a recorded .atd session first."); return; }
    if(backend.replayIndex+1>=backend.replayCount()) seekReplay(0);
    backend.replayPlaying=true; replayClock.restart();
  });
  connect(form.pauseButton,&QPushButton::clicked,this,[this] { backend.replayPlaying=false; });
  connect(form.stepButton,&QPushButton::clicked,this,[this] {
    backend.replayPlaying=false; seekReplay(std::min(backend.replayIndex+1,!backend.replayCount() ? std::size_t(0) : backend.replayCount()-1));
  });
  scrub=form.scrubber; replayTime=form.replayTime; replayTable=form.replayTable;
  scrub->setRange(0,std::max(0,int(backend.replayCount())-1)); scrub->setValue(int(backend.replayIndex));
  connect(scrub,&QSlider::sliderMoved,this,[this](int index) { seekReplay(index); });
  form.replayPath->setText(currentReplay.isEmpty() ? "Open a local recording to replay exact raw records." : currentReplay);
  registerPlot(form.replayPlot,"replay");
  populate(replayTable,{});
  form.loaded->setText(QString::number(backend.replayCount())); form.status->setText(eventText);
  form.profile->setText(replayProfileDescription);
  connect(form.addBookmarkButton,&QPushButton::clicked,this,[this] {
    if(!backend.replayCount()) return;
    const auto key=currentReplay+"/bookmarks"; auto list=settings.value(key).toStringList();
    list<<QString::number(backend.replayIndex); settings.setValue(key,list); navigate(11);
  });
  for(const auto &value:settings.value(currentReplay+"/bookmarks").toStringList()) {
    bool valid=false; const auto index=value.toULongLong(&valid);
    if(valid && index<backend.replayCount()) {
      auto item=new QListWidgetItem(stamp(backend.replayTimestamp(index))+" · Jump",form.bookmarkList);
      item->setData(Qt::UserRole,QVariant::fromValue<qulonglong>(index));
    }
  }
  connect(form.bookmarkList,&QListWidget::itemActivated,this,[this](QListWidgetItem *item) { seekReplay(item->data(Qt::UserRole).toULongLong()); });
  seekReplay(backend.replayIndex);
}


void Window::seekReplay(std::size_t index){
    if(!backend.replayCount())return;index=std::min(index,backend.replayCount()-1);
    if(!backend.recordingIndex){backend.seek(index);refreshReplayTable();refreshDataViews();return;}
    if(!replaySeekPending && index>=backend.replayIndex && index>=backend.replayWindowStart && std::min(index+7,backend.replayCount()-1)<backend.replayWindowStart+backend.replayRecords.size()) {
      for(auto i=backend.replayIndex+1;i<=index;++i)replayActivity.observe(backend.replayRecords[i-backend.replayWindowStart]);backend.replayIndex=index;backend.selected=backend.replayRecords[index-backend.replayWindowStart];backend.replayPositionNs=double(backend.recordingIndex->playbackTimestamp(index));
      if(scrub){QSignalBlocker guard(scrub);scrub->setValue(index);}if(replayTime)replayTime->setText(stamp(backend.selected.timestampNs)+" / "+stamp(backend.replayTimestamp(backend.replayCount()-1)));refreshReplayTable();refreshDataViews();refreshRtDashboard();return;
    }
    const auto generation=++*replayGeneration;replaySeekPending=true;
    struct Result {std::vector<aero::Record> records;aero::RtActivity activity;std::size_t start{},index{};QString error;};
    const auto recording=backend.recordingIndex;const auto checkpoints=replayCheckpoints;const auto cancel=replayGeneration;
    auto watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,generation,recording]{auto result=watcher->result();watcher->deleteLater();if(generation!=replayGeneration->load()||recording!=backend.recordingIndex)return;
      replaySeekPending=false;if(!result.error.isEmpty()){backend.replayPlaying=false;toast(result.error);return;}
      backend.replayWindowStart=result.start;backend.replayRecords=std::move(result.records);backend.replayIndex=result.index;backend.selected=backend.replayRecords.at(result.index-result.start);backend.replayPositionNs=double(recording->playbackTimestamp(result.index));replayActivity=result.activity;
      if(scrub){QSignalBlocker guard(scrub);scrub->setValue(backend.replayIndex);}if(replayTime)replayTime->setText(stamp(backend.selected.timestampNs)+" / "+stamp(backend.replayTimestamp(backend.replayCount()-1)));
      replayClock.restart();refreshReplayTable();refreshDataViews();refreshRtDashboard();
    });
    watcher->setFuture(QtConcurrent::run([recording,checkpoints,index,generation,cancel]{Result result;result.index=index;result.start=index>4096?index-4096:0;const auto finish=std::min(recording->size(),std::max(index+8,result.start+8192));
      try{result.records.reserve(finish-result.start);for(auto i=result.start;i<finish;++i){if(cancel->load()!=generation)return result;result.records.push_back(recording->at(i));}
        const auto block=checkpoints?index/4096:0;if(checkpoints&&block<checkpoints->size())result.activity=checkpoints->at(block);
        for(auto i=block*4096;i<=index;++i){if(cancel->load()!=generation)return result;result.activity.observe(i>=result.start&&i<finish?result.records[i-result.start]:recording->at(i));}
      }catch(const std::exception &e){result.error=QString::fromUtf8(e.what());}return result;}));
  }

void Window::refreshReplayTable(){
    if (!replayTable || replaySeekPending || backend.replayRecords.empty())
      return;
    replayTable->setRowCount(
        std::min<std::size_t>(7, backend.replayCount() - backend.replayIndex));
    try {for (int i = 0; i < replayTable->rowCount(); ++i) {
      auto r = backend.replayRecords.at(backend.replayIndex+i-backend.replayWindowStart);
      auto c = aero::command(r.commandWord);
      QStringList row = {stamp(r.timestampNs),           r.bus ? "B" : "A",
                         QString::number(c.rt),          QString::number(c.sa),
                         c.transmit ? "RT→BC" : "BC→RT", words(r)};
      for (int j = 0; j < 6; ++j)
        replayTable->setItem(i, j, new QTableWidgetItem(row[j]));
    }}catch(const std::exception &e){backend.replayPlaying=false;replayTable->setRowCount(0);toast(QString::fromUtf8(e.what()));}
  }

const std::vector<aero::Record> &Window::analysisData() const { return backend.data(model.records); }


std::size_t Window::analysisEnd() const { return backend.end(model.records); }


QStringList Window::engineeringValues() {
  const auto mappings=backend.mappings();
  const auto latest=backend.liveValues(model.records,analysisChannel,analysisBus);
  QStringList result;
  for(unsigned index=0;index<8;++index) {
    if(index>=mappings.size()) {result << "— Unmapped";continue;}
    const auto &mapping=mappings[index];
    const auto value=index<latest.size() ? latest[index].value : std::optional<double>{};
    result << (value ? QString::number(*value,'f',2)+" "+QString::fromStdString(mapping.unit) : "— No matching data");
  }
  return result;
}


void Window::refreshDataViews() {
  updateEngineeringDetails();
  if(!engineeringLabels.empty()) {
    const auto values=engineeringValues();
    for(int i=0;i<engineeringLabels.size();++i) {
      engineeringLabels[i]->setText(values[i]);
    }
    refreshEngineeringTable();
  }
  if(plotPaused || (page==7&&recordedCsv)) return;
  const auto mappings=backend.mappings();
  const auto frame=backend.replaySource?std::shared_ptr<const aero::LiveFrame>():backend.acquisition.liveFrame();
  for(auto plot:plots) {
    const auto index=plot->property("signalIndex").toInt();
    const auto channel=plot->property("sourceChannel").toUInt();
    const auto bus=plot->property("sourceBus").toInt();
    std::optional<double> minimum,maximum;
    if(plot->property("role").toString().isEmpty() && index>=0 && index<int(mappings.size())) {
      const auto value=backend.liveValue(index,model.records,channel,bus);
      const auto quality=!value.matched?trafficMismatch(mappings[index],channel,bus):value.value?QString("Decoded sample"):QString("Invalid sample · ")+QString::fromUtf8(value.reason.data(),qsizetype(value.reason.size()));
      const auto time=backend.exampleContext()?"Synthetic relative time":
        backend.replaySource?"Recorded stream time":backend.captureConfig.hardware?"Card time · per stream":"Synthetic relative time";
      const auto revision=backend.replaySource?replayProfileDescription:frame?QString("Profile revision %1").arg(frame->revision):QString("Active profile");
      const auto &signal=mappings[index];
      const auto &rules=backend.captureConfig.checkRules;
      const auto check=std::find_if(rules.begin(),rules.end(),[&](const auto &rule){return rule.signalId==signal.id;});
      const bool selectedStream=(!signal.channel || signal.channel==channel) && (signal.bus<0 || signal.bus==bus);
      const bool sameUnits=!backend.replaySource || std::any_of(backend.profile.begin(),backend.profile.end(),[&](const auto &current){
        return current.id==signal.id && current.unit==signal.unit && current.rt==signal.rt &&
          current.sa==signal.sa && current.transmit==signal.transmit;
      });
      if(check!=rules.end() && selectedStream && sameUnits){minimum=check->minimum;maximum=check->maximum;}
      QStringList limits;
      if(minimum)limits.append("Min "+desktop::displayValue(*minimum));
      if(maximum)limits.append("Max "+desktop::displayValue(*maximum));
      const auto thresholdText=limits.isEmpty()?QString():" · Configured limits "+limits.join(" / ")+
        " "+QString::fromStdString(signal.unit);
      plot->setStatus(QString("%1 · CH%2/%3 · %4 · %5").arg(quality).arg(channel).arg(bus==1?"B":"A",time,revision)+thresholdText);
    } else plot->setStatus({});
    const auto series=backend.series(plot->property("role").toString().toStdString(),plot->property("signalIndex").toUInt(),model.records,plot->property("sourceChannel").toUInt(),plot->property("sourceBus").toInt(),plot->property("rawColumn").toInt());
    QVector<QPointF> primary,secondary;
    primary.reserve(series.primary.size()); secondary.reserve(series.secondary.size());
    for(const auto &[x,y]:series.primary) primary << QPointF(x,y);
    for(const auto &[x,y]:series.secondary) secondary << QPointF(x,y);
    plot->setSeries(std::move(primary),std::move(secondary),minimum,maximum);
  }
}

