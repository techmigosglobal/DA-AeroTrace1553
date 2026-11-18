#include "main_window.hpp"
#include "services/key_gate.hpp"
#include "services/ddc_discovery.hpp"
#include "ui_welcome_page.h"
#include "ui_device_page.h"
#include "ui_dashboard_page.h"
#include "ui_monitor_page.h"
#include "ui_inspector_page.h"
#include "ui_engineering_page.h"
#include "ui_plots_page.h"
#include "ui_trace_card.h"
#include "ui_topology_page.h"
#include "ui_events_page.h"
#include "ui_license_page.h"

void Window::licensePage() {
    pageLayout->addSpacing(23);
    auto panel = new QFrame;
    Ui::LicensePage form;
    form.setupUi(panel);
    panel->setProperty("designerSource", desktop::pages.at(0).form);
    if (pendingPage != 1)
      form.error->setText(names[pendingPage] + " is locked. Open the local workspace to continue.");
    if(desktop::keyRequired()){form.primary->setText("Verify application key");form.error->setText(applicationKeyMessage);}
    connect(form.primary, &QPushButton::clicked, this, [this] {
      if(desktop::keyRequired()){verifyApplicationKey(true);return;}
      unlocked = true;
      licenseBadge->setText("KEY NOT VERIFIED");
      licenseBadge->setStyleSheet("color:#008663;background:#edf9f4;border-radius:14px;padding:6px 10px;font-size:10px");
      footerBadge->setText("● Local workspace");
      footerBadge->setStyleSheet("color:#008663;font-size:11px");
      navigate(pendingPage);
    });
    connect(form.exitButton, &QPushButton::clicked, this, &QWidget::close);
    pageLayout->addWidget(panel, 0, Qt::AlignHCenter);
    pageLayout->addStretch();
}

qsizetype Window::countBus(int bus){
    const auto metrics=backend.acquisition.metrics(false);return metrics.sourceCounts[bus]+metrics.sourceCounts[2+bus];
  }

QString Window::fileSize(QString p){
    return p.isEmpty()
               ? "0 B"
               : QString::number(QFileInfo(p).size() / 1024., 'f', 1) + " KB";
  }





void Window::liveMonitor() {
  if(!viewingReplayStatistics)backend.replaySource=false;
  if(!settings.contains("monitorRowMode")){settings.setValue("monitorRowMode","statistics");settings.setValue("monitorLayout","windows");}
  auto form = showForm<Ui::MonitorPage>();
  deviceTabs=form.deviceTabs;selectedSummary=form.selectedSummary;monitorCsv=form.csvButton;
  deviceTitles={form.deviceTitle0,form.deviceTitle1};deviceStatuses={form.deviceStatus0,form.deviceStatus1};
  deviceTabs->addTab("Both channels");
  deviceTabs->addTab(QString("Device %1 · External").arg(backend.captureConfig.logicalDevices[0]));
  deviceTabs->addTab(QString("Device %1 · Internal").arg(backend.captureConfig.logicalDevices[1]));
  deviceTabs->setExpanding(false);deviceTabs->setCurrentIndex(settings.value("monitorTab",model.channelFilter).toInt());model.channelFilter=deviceTabs->currentIndex();model.refresh();
  connect(deviceTabs,&QTabBar::currentChanged,this,[this,form](int channel) {
    const auto old=QString("tab%1").arg(model.channelFilter);settings.setValue(old+"Bus",model.busFilter);settings.setValue(old+"Rt",model.rtFilter);settings.setValue(old+"Search",model.search);
    model.channelFilter=channel;settings.setValue("monitorTab",channel);const auto next=QString("tab%1").arg(channel);
    model.busFilter=settings.value(next+"Bus").toString();model.rtFilter=settings.value(next+"Rt",-1).toInt();model.search=settings.value(next+"Search").toString();
    form.rtFilter->setCurrentIndex(model.rtFilter+1);form.hexSearch->setText(model.search);
    (model.busFilter=="A"?form.busAButton:model.busFilter=="B"?form.busBButton:form.allBusButton)->setChecked(true);
    model.refresh();refreshOperatorState();
  });
  form.pauseButton->hide();
  recordButton=new QPushButton("Start recording");recordButton->setObjectName("startRecordingButton");
  form.bodyLayout->insertWidget(0,recordButton);connect(recordButton,&QPushButton::clicked,this,[this]{if(backend.acquisition.active())stopAndSave();else startCapture();});
  form.bodyLayout->insertWidget(0,saveControls());
  form.bodyLayout->insertWidget(0,connectionResultPanel());
  connect(form.inspectButton, &QPushButton::clicked, this, [this] { inspectSelection(); });
  const QList<QPushButton *> buses = {form.allBusButton,form.busAButton,form.busBButton};
  for (int i=0;i<buses.size();++i) connect(buses[i], &QPushButton::clicked, this, [this,i] {
    model.busFilter = i==0 ? "" : i==1 ? "A" : "B"; model.refresh();
  });
  form.rtFilter->setCurrentIndex(model.rtFilter+1);
  form.hexSearch->setText(model.search);
  connect(form.rtFilter, &QComboBox::currentIndexChanged, this, [this](int index) { model.rtFilter=index-1; model.refresh(); });
  connect(form.hexSearch, &QLineEdit::textChanged, this, [this](const QString &text) { model.search=text.trimmed(); model.refresh(); });
  monitor = form.monitor; monitorStatus = form.monitorStatus;monitorStatus->setWordWrap(true);
  monitor->setModel(&model);
  monitor->verticalHeader()->hide();
  monitor->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  monitor->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
  monitor->horizontalHeader()->setStretchLastSection(false);
  monitor->horizontalHeader()->setSectionResizeMode(8,QHeaderView::Stretch);
  monitor->verticalHeader()->setDefaultSectionSize(settings.value("dense",false).toBool() ? 30 : 38);
  const QList<int> widths={58,164,84,56,46,46,100,50,320,90,92};
  for(int i=0;i<model.columnCount();++i) {
    monitor->setColumnWidth(i,i<widths.size() ? widths[i] : 110);
    monitor->setColumnHidden(i,settings.value(QString("monitorColumn%1").arg(i),i!=7 && i!=9 && i<11).toBool()==false);
  }
  auto columns=new QMenu(form.columnsButton);
  auto wordColumns=columns->addMenu("Individual data words");
  for(int i=0;i<model.columnCount();++i) {
    auto action=(i>=17 ? wordColumns : columns)->addAction(model.headerData(i,Qt::Horizontal,Qt::DisplayRole).toString());action->setCheckable(true);action->setChecked(!monitor->isColumnHidden(i));
    connect(action,&QAction::toggled,monitor,[this,i](bool show){if(monitor) monitor->setColumnHidden(i,!show);settings.setValue(QString("monitorColumn%1").arg(i),show);});
  }
  form.columnsButton->setMenu(columns);
  connect(form.csvButton,&QPushButton::clicked,this,[this]{stopAndSave();});
  form.csvButton->setText("Stop && save");
  form.csvButton->setToolTip("Stop, drain and finalize the session, then save complete Internal and External CSVs. Display filters do not restrict this export.");
  connect(form.engineeringButton,&QPushButton::clicked,this,[this]{if(monitor) if(auto record=selectedMonitorRecord()) {backend.selected=*record;analysisChannel=record->channel;analysisBus=record->bus;}navigate(6);});
  connect(form.plotButton,&QPushButton::clicked,this,[this]{plotMonitorSelection();});
  connect(monitor->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](const QModelIndex &index){
    if(const auto record=model.at(index.row())) {selectedMonitorId=record->id;selectedMonitorColumn=index.column();backend.selected=*record;}
    refreshOperatorState();
  });
  (model.busFilter=="A" ? form.busAButton : model.busFilter=="B" ? form.busBButton : form.allBusButton)->setChecked(true);
  connect(monitor, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {if(index.column()>=17) plotMonitorSelection();else inspectSelection();});
  if (model.rowCount()) monitor->selectRow(0);
  monitor->horizontalHeader()->restoreState(settings.value("monitorHeader").toByteArray());
  buildMonitorPanes(form.monitor);
  form.csvButton->hide();
}


void Window::inspectSelection(){
    if (monitor) {
      auto r = selectedMonitorRecord();
      if (r)
        backend.selected = *r;
    }
    navigate(5);
  }

void Window::inspector() {
  auto form = showForm<Ui::InspectorPage>();
  connect(form.previousButton, &QPushButton::clicked, this, [this] { selectRelative(-1); });
  connect(form.nextButton, &QPushButton::clicked, this, [this] { selectRelative(1); });
  const auto command = aero::command(backend.selected.commandWord);
  const auto protocol=aero::interpret(backend.selected);
  form.clock->setText(!backend.selected.id ? "No captured frame" : backend.selected.source ? (backend.selected.source==2 ? "Legacy MT 2 us ticks; rollover ambiguity flagged" : "DDC Chapter 10 device time") : "Simulator time");
  form.selectedRecord->setText("#"+QString::number(backend.selected.id));
  form.selectedRecordHint->setText(stamp(backend.selected.timestampNs));
  form.selectedSource->setText(QString("%1 / %2").arg(backend.selected.channel).arg(backend.selected.bus ? "B" : "A"));
  form.transaction->setText(QString::fromUtf8(protocol.transaction.data(),qsizetype(protocol.transaction.size())));
  form.transactionHint->setText(QString("Payload RT %1 · SA %2 · expected/actual %3/%4").arg(protocol.payloadCommand.rt).arg(protocol.payloadCommand.sa).arg(protocol.expectedWords).arg(protocol.actualWords));
  form.transactionHint->setToolTip(QString::fromStdString(aero::protocolDiagnostics(backend.selected,protocol)));
  form.selectedIntegrity->setText(!backend.selected.id ? "No frame" : !protocol.supported ? "Unsupported / insufficient evidence" : !protocol.valid ? "Review message structure" : (protocol.terminalBusy || protocol.terminalFault || protocol.terminalMessageError) ? "Received · RT status reported" : !protocol.sdkEvidenceAvailable ? "Structure inferred · SDK evidence unavailable" : "Structure checks passed");
  form.selectedIntegrity->setToolTip(QString::fromStdString(aero::protocolDiagnostics(backend.selected,protocol)));
  form.commandWord->setText("0x"+hexword(backend.selected.commandWord));
  form.rt->setText(QString::number(command.rt)); form.direction->setText(QString::number(command.transmit));
  form.sa->setText(QString::number(command.sa)); form.count->setText(QString::number(command.count));
  form.countCaption->setText(command.mode ? "MODE" : "WC");
  form.rtHint->setText(QString::number(command.rt,2).rightJustified(5,'0'));
  form.saHint->setText(QString::number(command.sa,2).rightJustified(5,'0'));
  form.timestamp->setText(stamp(backend.selected.timestampNs));
  form.recordSource->setText(QString("Channel %1 · Bus %2").arg(backend.selected.channel).arg(backend.selected.bus ? "B" : "A"));
  form.status->setText("0x"+hexword(backend.selected.statusWord));form.status->setToolTip(QString::fromStdString(aero::statusDescription(protocol)));
  form.flags->setText(QString::number(backend.selected.flags));form.flags->setToolTip(QString::fromStdString(aero::protocolDiagnostics(backend.selected,protocol)));
  if(protocol.mode)form.count->setToolTip(QString::fromUtf8(protocol.modeName.data(),qsizetype(protocol.modeName.size())));
  form.clock->setToolTip("Response time and intermessage gap: not measured. Device timestamp encoding does not establish physical measurement accuracy or synchronized channels.");
  form.mapping->setText(QString::fromStdString(backend.signal.id));
  QList<QStringList> rows;
  for (int i=0;i<int(backend.selected.words.size());++i) rows << QStringList{QString::number(i),"0x"+hexword(backend.selected.words[i]),QString::number(backend.selected.words[i]),QString::number(backend.selected.words[i],2).rightJustified(16,'0')};
  populate(form.rawTable,rows);
  const auto value = aero::decode(backend.selected,backend.signal,backend.exampleContext());
  populate(form.engineeringTable,{{QString::fromStdString(backend.signal.id),value ? QString::number(*value,'f',2) : "—",QString::fromStdString(backend.signal.unit),value ? "VALID" : protocol.payloadEligible ? "NO MATCH / INVALID MAPPING" : QString::fromUtf8(protocol.reason.data(),qsizetype(protocol.reason.size()))}});
}


void Window::selectRelative(int delta){
    if(backend.replaySource&&backend.recordingIndex){seekReplay(std::size_t(qMax<qint64>(0,qint64(backend.replayIndex)+delta)));navigate(5);return;}
    auto &data = backend.replayRecords.empty() ? model.records : backend.replayRecords;
    for (int i = 0; i < data.size(); ++i)
      if (data[i].id == backend.selected.id) {
        backend.selected = data[qBound(0, i + delta, int(data.size() - 1))];
        break;
      }
    navigate(5);
  }

void Window::plotMonitorSelection() {
  recordedCsv.reset();
  const bool grouped=rtGrouped&&monitorWorkspace&&monitorWorkspace->isVisible();
  if(!grouped&&(!monitor || !monitor->currentIndex().isValid())) {toast("Select a record or signal to plot.");return;}
  const auto record=selectedMonitorRecord();if(!record) return;
  backend.selected=*record;analysisChannel=record->channel;analysisBus=record->bus;
  const auto column=grouped?8:monitor->currentIndex().column(),word=column-17;const auto cmd=aero::interpret(*record).payloadCommand;
  const auto mappings=backend.mappings();plotRawColumn=-1;bool found=false;
  if(column==1) {toast("Card time is the x-axis. Select a value or individual word to plot against it.");return;}
  if(column==8 || column>=17) for(unsigned i=0;i<mappings.size();++i) {
    const auto &signal=mappings[i];
    if(signal.rt!=cmd.rt || signal.sa!=cmd.sa || signal.transmit!=cmd.transmit || (signal.channel && signal.channel!=record->channel) || (signal.bus>=0 && signal.bus!=record->bus)) continue;
    if(column>=17 && (word<int(signal.word) || word>=int(signal.word+(signal.lsb+signal.width+15)/16))) continue;
    preferredSignalIndex=i;plotPanelIds[0]=QString::fromStdString(mappings[i].id);found=true;break;
  }
  if(!found && column!=8) plotRawColumn=column;
  if(!found && column==8) {toast("Select an individual Word column or a matching ICD signal to plot.");return;}
  if(!found && plotRawColumn<0 && mappings.empty()) {toast("Import an ICD or select an individual Word column to plot raw values.");return;}
  applicationEvent("plot_selection",QString("channel=%1 bus=%2 column=%3 mapped=%4").arg(analysisChannel).arg(analysisBus).arg(column).arg(found));navigate(7);
}

void Window::refreshEngineeringTable() {
  if(!qualityTable) return;
  const auto mappings=backend.mappings();const auto values=backend.liveValues(model.records,analysisChannel,analysisBus);
  for(unsigned row=0;row<mappings.size() && row<unsigned(qualityTable->rowCount());++row) {
    qualityTable->setRowHidden(int(row),!inWorkspace(mappings[row]));
    const auto value=row<values.size() ? values[row] : aero::LiveValue{};QStringList raw;
    for(unsigned i=0;i<value.rawCount;++i) raw<<hexword(value.raw[i]);
    const QStringList cells={QString::fromStdString(mappings[row].sourceSignalId.empty()?mappings[row].id:mappings[row].sourceSignalId),raw.join(' '),value.value ? QString::number(*value.value,'g',12)+" "+QString::fromStdString(mappings[row].unit) : "—",value.matched ? stamp(value.timestampNs) : "—",value.value ? "VALID" : value.matched ? QString::fromUtf8(value.reason.data(),qsizetype(value.reason.size())) : trafficMismatch(mappings[row],analysisChannel,analysisBus)};
    for(int column=0;column<cells.size();++column) {auto item=qualityTable->item(row,column);if(!item) {item=new QTableWidgetItem;qualityTable->setItem(row,column,item);}if(item->text()!=cells[column]) {item->setText(cells[column]);item->setToolTip(cells[column]);}}
  }
  updateEngineeringDetails();
}

void Window::addEngineeringActions() {
  auto report=new QPushButton("Export signal report…",content);
  titleActions->addWidget(report);
  connect(report,&QPushButton::clicked,this,[this]{
    const auto mappings=backend.mappings();
    exportSignalReport(preferredSignalIndex<mappings.size()?QString::fromStdString(mappings[preferredSignalIndex].id):QString{});
  });
  if(!backend.replaySource && backend.captureConfig.hardware &&
      std::any_of(backend.profile.begin(),backend.profile.end(),[](const auto &signal){return signal.simulationOnly;})) {
    auto review=new QPushButton("Review signals…",content);
    auto notice=new QLabel("This profile contains simulation-only mappings. Publish the ICD for your bus traffic to enable hardware engineering values.",content);
    notice->setWordWrap(true);
    pageLayout->addWidget(notice);
    titleActions->addWidget(review);
    connect(review,&QPushButton::clicked,this,[this]{reviewSignals();});
  }
}

void Window::plotPage() {
  if(recordedCsv){recordedCsvPage();return;}
  auto form=showForm<Ui::PlotsPage>();
  form.plotSource->addItems({"External · CH1 / A","External · CH1 / B","Internal · CH2 / A","Internal · CH2 / B"});form.plotSource->setCurrentIndex((analysisChannel-1)*2+analysisBus);
  connect(form.plotSource,&QComboBox::currentIndexChanged,this,[this](int source){analysisChannel=source/2+1;analysisBus=source%2;for(auto plot:plots) {plot->setProperty("sourceChannel",analysisChannel);plot->setProperty("sourceBus",analysisBus);}refreshDataViews();});
  const auto mappings=backend.mappings();
  form.plotSignal->setVisible(plotRawColumn>=0);
  if(plotRawColumn>=0) form.plotSignal->addItem("Raw · "+model.headerData(plotRawColumn,Qt::Horizontal,Qt::DisplayRole).toString(),-1);
  for(unsigned i=0;i<mappings.size();++i)if(inWorkspace(mappings[i]))form.plotSignal->addItem(QString::fromStdString(mappings[i].id),int(i));
  if(preferredSignalIndex>=mappings.size()) preferredSignalIndex=0;
  form.plotSignal->setCurrentIndex(plotRawColumn>=0 ? 0 : form.plotSignal->findData(int(preferredSignalIndex)));
  connect(form.plotSignal,&QComboBox::currentIndexChanged,this,[this,combo=form.plotSignal](int){const auto signal=combo->currentData().toInt();if(signal>=0) {preferredSignalIndex=signal;plotPanelIds[0]=combo->currentText();plotRawColumn=-1;QTimer::singleShot(0,this,[this]{navigate(7);});}});
  form.pausePlotsButton->setText(plotPaused ? "Resume plots" : "Pause plots");
  connect(form.pausePlotsButton,&QPushButton::clicked,this,[this,button=form.pausePlotsButton] {
    plotPaused=!plotPaused; button->setText(plotPaused ? "Resume plots" : "Pause plots");
    if (!plotPaused) refreshDataViews();
  });
  connect(form.exportPlotButton,&QPushButton::clicked,this,[this] { exportPlot(); });
  cursorALabel=form.cursorA; cursorBLabel=form.cursorB; cursorDeltaLabel=form.cursorDelta;
  connect(form.cursorInspectButton,&QPushButton::clicked,this,[this] { navigate(5); });
  const QList<Plot *> initial={form.signalPlot0,form.signalPlot1,form.signalPlot2,form.signalPlot3};
  if(!plotPanelsInitialized && !mappings.empty()) {
    for(unsigned i=0;i<std::min<std::size_t>(4,mappings.size());++i)plotPanelIds[i]=QString::fromStdString(mappings[i].id);
    if(plotPanelIds[0].isEmpty() && preferredSignalIndex<mappings.size())plotPanelIds[0]=QString::fromStdString(mappings[preferredSignalIndex].id);
    plotPanelsInitialized=true;
  }
  if(preferredSignalIndex<mappings.size() && inWorkspace(mappings[preferredSignalIndex]))plotPanelIds[0]=QString::fromStdString(mappings[preferredSignalIndex].id);
  const QList<QComboBox *> selectors={form.panel0Select,form.panel1Select,form.panel2Select,form.panel3Select};
  const QList<QPushButton *> expanders={form.panel0Expand,form.panel1Expand,form.panel2Expand,form.panel3Expand};
  const QList<QFrame *> cards={form.plotCard0,form.plotCard1,form.plotCard2,form.plotCard3};
  form.addSignalButton->setVisible(false);form.restorePlotsButton->setText("Compare");form.restorePlotsButton->show();
  for(int i=1;i<cards.size();++i)cards[i]->hide();
  const auto expand=[cards,restore=form.restorePlotsButton](int selected){for(int i=0;i<cards.size();++i)cards[i]->setVisible(i==selected);restore->show();};
  form.restorePlotsButton->setCheckable(true);connect(form.restorePlotsButton,&QPushButton::toggled,this,[cards](bool compare){for(int i=0;i<cards.size();++i)cards[i]->setVisible(i==0||compare);});
  const auto updateTraces=[this,initial]{std::vector<unsigned> selected;for(auto plot:initial)if(plot->property("role").toString().isEmpty()&&plot->property("signalIndex").toInt()>=0)selected.push_back(plot->property("signalIndex").toUInt());backend.acquisition.setTraceSignals(selected);refreshDataViews();};
  for(int i=0;i<initial.size();++i) {
    auto combo=selectors[i];combo->setAccessibleName(QString("Plot %1 signal").arg(i+1));
    combo->addItem("No signal",QString());
    for(const auto &mapping:mappings){
      if(!inWorkspace(mapping))continue;
      const auto id=QString::fromStdString(mapping.id);
      const auto unit=QString::fromStdString(mapping.unit);
      combo->addItem(unit.isEmpty()?id:id+" · "+unit,id);
    }
    const auto indexOf=[mappings](const QString &id){
      const auto it=std::find_if(mappings.begin(),mappings.end(),[&](const auto &signal){return QString::fromStdString(signal.id)==id;});
      return it==mappings.end()?-1:int(it-mappings.begin());
    };
    auto index=indexOf(plotPanelIds[i]);
    if(index<0 || !inWorkspace(mappings[index])){plotPanelIds[i].clear();index=-1;}
    combo->setCurrentIndex(qMax(0,combo->findData(plotPanelIds[i])));
    registerPlot(initial[i],i==0&&plotRawColumn>=0?"raw_column":"",index);
    if(i==0&&plotRawColumn>=0)initial[i]->setProperty("rawColumn",plotRawColumn);
    configureCursor(initial[i]);
    auto title=content->findChild<QLabel *>(QString("plotCard%1Title").arg(i));
    auto updateTitle=[this,title,combo,i]{if(title)title->setText(i==0&&plotRawColumn>=0?"Raw · "+model.headerData(plotRawColumn,Qt::Horizontal,Qt::DisplayRole).toString():combo->currentIndex()>0?combo->currentText():"Select a signal");};updateTitle();
    connect(combo,&QComboBox::currentIndexChanged,this,[this,i,combo,plot=initial[i],updateTitle,updateTraces,indexOf,source=form.plotSource](int){
      plotPanelIds[i]=combo->currentData().toString();
      const auto index=indexOf(plotPanelIds[i]);
      if(i==0){plotRawColumn=-1;if(index>=0){preferredSignalIndex=unsigned(index);const auto rules=backend.mappings();const auto &mapping=rules[index];if(mapping.channel)analysisChannel=mapping.channel;if(mapping.bus>=0)analysisBus=mapping.bus;source->setCurrentIndex((analysisChannel-1)*2+analysisBus);}}
      plot->setProperty("role","");plot->setProperty("signalIndex",index);
      cursorAValue.reset();updateTitle();updateTraces();
    });
    connect(expanders[i],&QPushButton::clicked,this,[expand,i]{expand(i);});

  }
  // One compact control row; the chart owns the remaining vertical space.
  auto header=form.actionBar->parentWidget();
  form.bodyLayout->insertWidget(0,form.actionBar);if(header&&header!=form.actionBar->parentWidget())header->hide();
  form.actionBarLayout->setContentsMargins(0,0,0,0);form.actionBarLayout->setSpacing(5);
  form.actionBar->setStyleSheet("QComboBox,QPushButton,QToolButton {padding:4px 7px;min-height:20px;}");
  form.panel0Controls->hide();form.actionBarLayout->insertWidget(1,form.panel0Select,1);
  for(auto button:expanders)button->hide();
  for(auto card:cards)if(auto l=qobject_cast<QVBoxLayout *>(card->layout()))l->setContentsMargins(4,4,4,4);
  form.plotsGrid->setContentsMargins(0,0,0,0);form.bodyLayout->setContentsMargins(0,0,0,0);
  form.cursorTools->layout()->setContentsMargins(0,0,0,0);form.cursorTools->setStyleSheet("QPushButton {padding:3px;min-height:18px;}");
  updateTraces();
  configureAnalysisTools();

}


void Window::exportPlot(){
    auto path = QFileDialog::getSaveFileName(
        this, "Export plot", sessionDir + "/plot.png", "PNG (*.png)");
    if (path.isEmpty())
      return;
    auto surface=content->findChild<QWidget *>("plotSurface");
    if ((surface?surface:content)->grab().save(path))
      toast("Plot workspace exported to " + path);
    else
      toast("Could not save plot image.");
  }

void Window::topology() {
  auto form=showForm<Ui::TopologyPage>();
  connect(form.selectBusButton,&QPushButton::clicked,this,[this] { navigate(4); });
  connect(form.liveMonitorButton,&QPushButton::clicked,this,[this] { navigate(4); });
  const QList<QPushButton *> nodes={form.node0_0,form.node0_1,form.node0_2,form.node0_3,form.node0_4,form.node0_5,form.node0_6,form.node0_7,form.node1_0,form.node1_1,form.node1_2,form.node1_3,form.node1_4,form.node1_5,form.node1_6,form.node1_7};
  for(int i=0;i<nodes.size();++i) connect(nodes[i],&QPushButton::clicked,this,[this,i,node=nodes[i]] {
    model.busFilter=i/8 ? "B" : "A"; model.rtFilter=node->property("observedRt").toInt(); model.refresh(); navigate(4);
  });
  const auto refresh=[this,form,nodes] {
    std::array<QSet<int>,2> addresses; QSet<int> all;
    const auto &records=backend.data(model.records);
    for(auto i=records.size()>5000 ? records.size()-5000 : 0;i<records.size();++i) {
      if(records[i].bus>1 || records[i].flags) continue;
      const auto rt=aero::command(records[i].commandWord).rt;
      if(rt<31) {addresses[records[i].bus].insert(rt);all.insert(rt);}
    }
    for(int bus=0;bus<2;++bus) {
      auto observed=addresses[bus].values();std::sort(observed.begin(),observed.end());
      for(int slot=0;slot<8;++slot) {
        auto node=nodes[bus*8+slot]; const bool present=slot==0 ? !observed.empty() : slot<=observed.size();
        node->setEnabled(present);node->setProperty("observedRt",slot && present ? observed[slot-1] : -1);
        node->setText(slot==0 ? "BC traffic" : present ? QString("RT %1").arg(observed[slot-1],2,10,QChar('0')) : "Unobserved");
      }
    }
    form.addresses->setText(QString::number(all.size()));
    form.addressesHint->setText("Last 5,000 records · first seven RTs per bus shown");
    form.messages->setText(QString::number(backend.acquisition.metrics(false).recorded));
    form.channels->setText(QString::number(std::count(backend.captureConfig.enabled.begin(),backend.captureConfig.enabled.end(),true)));
  };
  refresh(); connect(&tick,&QTimer::timeout,content,refresh);
}


void Window::events() {
  auto form=showForm<Ui::EventsPage>();
  connect(form.exportEventsButton,&QPushButton::clicked,this,[this] { exportEvents(); });
  connect(form.eventInspectorButton,&QPushButton::clicked,this,[this] { navigate(5); });
  auto refresh=[this,form] {
    const auto metrics=backend.acquisition.metrics(true);form.warnings->setText(QString::number(metrics.checkCount));form.information->setText(eventText.isEmpty()?"0":"1");form.drops->setText(QString::number(metrics.displayDrops));
    QList<QStringList> rows;for(const auto &e:metrics.checkEvents) {
      const auto kind=QString::fromStdString(e.kind);const int rule=kind=="protocol"?0:kind=="status"?1:(kind=="missing" || kind=="recovered")?2:(kind=="range" || kind=="range_recovered")?3:-1;
      if(rule>=0 && !settings.value(QString("eventRule%1").arg(rule),true).toBool())continue;
      rows<<QStringList{stamp(e.timestampNs),kind,QString("CH%1/%2 %3 · %4").arg(e.channel).arg(e.bus?'B':'A').arg(QString::fromStdString(e.signalId),QString::fromStdString(e.detail)),QString::number(e.recordId)};
    }
    if(!eventText.isEmpty())rows<<QStringList{QTime::currentTime().toString(),"Information",eventText,"Local"};populate(form.eventsTable,rows);
  };refresh();registerPlot(form.eventsPlot,"events");
  auto timer=new QTimer(form.eventsTable);timer->setInterval(500);connect(timer,&QTimer::timeout,form.eventsTable,refresh);timer->start();
  connect(form.eventsTable,&QTableWidget::cellDoubleClicked,this,[this,form](int index,int) {
    const auto item=form.eventsTable->item(index,3);if(!item)return;const auto id=item->text().toULongLong();
    for(const auto &record:backend.archive)if(id && record.id==id){backend.selected=record;navigate(5);return;}toast("This event has no retained message. Open its recording for analysis.");
  });
  form.rulesTitle->setText("Display filters");form.rulesNotice->setText("Recent 512 events appear here. The complete capture log is stored as .checks.jsonl.");
  auto editRules=new QPushButton("Configure signal alerts…",content);
  pageLayout->addWidget(editRules);
  connect(editRules,&QPushButton::clicked,this,[this]{configureSignalAlerts();});
  const QList<QCheckBox *> rules={form.protocolRule,form.statusRule,form.missingRule,form.rangeRule,form.diskRule};
  for(int i=0;i<rules.size();++i) {
    const auto key=QString("eventRule%1").arg(i);
    rules[i]->setChecked(settings.value(key,true).toBool());
    rules[i]->setEnabled(i<4);
    if(i<4) connect(rules[i],&QCheckBox::toggled,form.eventsTable,[this,key,refresh](bool on) { settings.setValue(key,on);refresh(); });
  }
}


void Window::exportEvents(){
    QString p = QFileDialog::getSaveFileName(
        this, "Export events", sessionDir + "/events.csv", "CSV (*.csv)");
    if (p.isEmpty())
      return;
    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
      toast(f.errorString());
      return;
    }
    f.write("timestamp_ns,record,channel,bus,signal,event,detail\n");
    const auto quote=[](QString value){return "\""+value.replace('"',"\"\"")+"\"";};
    for(const auto &e:backend.acquisition.metrics(true).checkEvents)f.write((QString::number(e.timestampNs)+","+QString::number(e.recordId)+","+QString::number(e.channel)+","+(e.bus?"B":"A")+","+quote(QString::fromStdString(e.signalId))+","+quote(QString::fromStdString(e.kind))+","+quote(QString::fromStdString(e.detail))+"\n").toUtf8());
    toast(f.commit() ? "Event list exported" : "Export failed");
  }

QString Window::trafficMismatch(const aero::Signal &signal,unsigned channel,int bus) const {
  if((signal.channel && signal.channel!=channel) || (signal.bus>=0 && signal.bus!=bus))
    return "Signal is assigned to a different stream";
  if(!backend.replaySource && channel>=1 && channel<=2 && bus>=0 && bus<2 &&
      !backend.captureConfig.enabled[(channel-1)*2+bus]) return "Selected stream is disabled";
  if(signal.simulationOnly && !backend.exampleContext())
    return backend.captureConfig.hardware && !backend.replaySource ?
      "Simulation-only profile · publish the hardware ICD" : "Waiting for verified Offline simulation traffic";
  if(backend.replaySource ? backend.replayRecords.empty() : backend.archive.empty())
    return "Waiting for traffic";
  const auto words=signal.expectedWordCount?QString(" · %1 words").arg(signal.expectedWordCount):QString{};
  return QString("No RT %1 / SA %2 %3 traffic%4")
    .arg(signal.rt).arg(signal.sa).arg(signal.transmit?"Tx":"Rx",words);
}


void Window::configureSignalAlerts() {
    if(backend.replaySource){toast("Return to the live workspace to edit signal limits; replay retains its own profile.");return;}
    if(connecting || backend.acquisition.active()) {toast("Stop capture before changing signal alerts.");return;}
    const auto mappings=backend.captureMappings();
    if(mappings.empty()){toast("Publish an ICD profile before configuring signal alerts.");return;}
    QDialog dialog(this);
    dialog.setWindowTitle("Signal alerts");
    auto layout=new QVBoxLayout(&dialog);
    auto help=new QLabel("Enter optional limits and hysteresis in engineering units, and the missing interval in seconds.");
    help->setWordWrap(true);layout->addWidget(help);
    auto grid=new QTableWidget(int(mappings.size()),5,&dialog);
    grid->setHorizontalHeaderLabels({"Signal","Minimum","Maximum","Hysteresis","Missing (s)"});
    grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(grid);
    for(int row=0;row<grid->rowCount();++row) {
      const auto &id=mappings[row].id;
      auto name=new QTableWidgetItem(QString::fromStdString(id));
      name->setFlags(name->flags() & ~Qt::ItemIsEditable);grid->setItem(row,0,name);
      for(int col=1;col<5;++col) grid->setItem(row,col,new QTableWidgetItem);
      for(const auto &rule:backend.captureConfig.checkRules) if(rule.signalId==id) {
        if(rule.minimum) grid->item(row,1)->setText(QString::number(*rule.minimum,'g',16));
        if(rule.maximum) grid->item(row,2)->setText(QString::number(*rule.maximum,'g',16));
        if(rule.hysteresis) grid->item(row,3)->setText(QString::number(rule.hysteresis,'g',16));
        if(rule.missingNs) grid->item(row,4)->setText(QString::number(rule.missingNs/1e9,'g',16));
      }
    }
    auto status=new QLabel;status->setWordWrap(true);layout->addWidget(status);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
      std::vector<aero::CheckRule> rules;
      for(int row=0;row<grid->rowCount();++row) {
        aero::CheckRule rule;rule.signalId=mappings[row].id;
        for(int col=1;col<5;++col) {
          const auto text=grid->item(row,col)->text().trimmed();
          if(text.isEmpty()) continue;
          bool ok=false;const auto value=text.toDouble(&ok);
          if(!ok || !std::isfinite(value) || (col==4 && (value<0 || value>86400))) {
            status->setText("Enter a valid number. Missing interval must be between 0 and 86400 seconds.");
            grid->setCurrentCell(row,col);return;
          }
          if(col==1) rule.minimum=value;
          if(col==2) rule.maximum=value;
          if(col==3) rule.hysteresis=value;
          if(col==4) rule.missingNs=std::uint64_t(std::round(value*1e9));
        }
        if(rule.minimum || rule.maximum || rule.hysteresis || rule.missingNs) rules.push_back(std::move(rule));
      }
      if(auto error=aero::validateChecks(rules,mappings);!error.empty()) {
        status->setText(QString::fromStdString(error));return;
      }
      backend.captureConfig.checkRules=std::move(rules);
      dialog.accept();
    });
    dialog.resize(760,460);
    dialog.exec();

  refreshDataViews();
}
