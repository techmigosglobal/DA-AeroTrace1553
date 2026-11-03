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
  backend.replaySource=false;
  if(!settings.contains("monitorRowMode")){settings.setValue("monitorRowMode","rt");settings.setValue("monitorLayout","windows");}
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
  form.selectedIntegrity->setText(!backend.selected.id ? "No frame" : protocol.valid ? "Validated message" : "Review protocol");
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
  const auto value = aero::decode(backend.selected,backend.signal);
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
    preferredSignalIndex=i;found=true;break;
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
    const auto value=row<values.size() ? values[row] : aero::LiveValue{};QStringList raw;
    for(unsigned i=0;i<value.rawCount;++i) raw<<hexword(value.raw[i]);
    const QStringList cells={QString::fromStdString(mappings[row].id),raw.join(' '),value.value ? QString::number(*value.value,'g',12)+" "+QString::fromStdString(mappings[row].unit) : "—",value.matched ? stamp(value.timestampNs) : "—",value.value ? "VALID" : value.matched ? QString::fromUtf8(value.reason.data(),qsizetype(value.reason.size())) : "NO MATCH"};
    for(int column=0;column<cells.size();++column) {auto item=qualityTable->item(row,column);if(!item) {item=new QTableWidgetItem;qualityTable->setItem(row,column,item);}if(item->text()!=cells[column]) {item->setText(cells[column]);item->setToolTip(cells[column]);}}
  }
}

void Window::engineering() {
  auto form = showForm<Ui::EngineeringPage>();
  form.analysisSource->addItems({"External · CH1 / A","External · CH1 / B","Internal · CH2 / A","Internal · CH2 / B"});
  form.analysisSource->setCurrentIndex((analysisChannel-1)*2+analysisBus);
  connect(form.analysisSource,&QComboBox::currentIndexChanged,this,[this](int source) {analysisChannel=source/2+1;analysisBus=source%2;for(auto plot:plots) {plot->setProperty("sourceChannel",analysisChannel);plot->setProperty("sourceBus",analysisBus);}refreshDataViews();});
  connect(form.loadProfileButton, &QPushButton::clicked, this, [this] { importIcd(); });
  form.openPlotsButton->setText("Plot selected signal");
  connect(form.openPlotsButton, &QPushButton::clicked, this, [this] {plotRawColumn=-1;navigate(7);});
  form.profileBadge->setText(backend.mappings().empty() ? "No active ICD profile" : QString("Active ICD: %1 signals").arg(backend.mappings().size()));
  engineeringLabels={form.value0,form.value1,form.value2,form.value3,form.value4,form.value5,form.value6,form.value7};
  qualityTable=form.qualityTable;
  const auto values=engineeringValues(); const auto mappings=backend.mappings();
  for (int i=0;i<8;++i) {
    engineeringLabels[i]->setText(values[i]);
    const auto name=i<int(mappings.size()) ? QString::fromStdString(mappings[i].id) : "Unmapped";
    if(auto caption=content->findChild<QLabel *>(QString("value%1Caption").arg(i))) caption->setText(name);
    if(auto hint=content->findChild<QLabel *>(QString("value%1Hint").arg(i))) hint->setText(i<int(mappings.size()) ? QString("RT %1 / SA %2 · Word %3").arg(mappings[i].rt).arg(mappings[i].sa).arg(mappings[i].word) : "Import a validated mapping");
  }
  qualityTable->setColumnCount(5);qualityTable->setHorizontalHeaderLabels({"Signal","Raw hex","Value / unit","Card time","Quality"});
  qualityTable->setRowCount(mappings.size());qualityTable->verticalHeader()->hide();qualityTable->verticalHeader()->setDefaultSectionSize(38);
  qualityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  const int widths[]={100,80,100,140,80};for(int i=0;i<5;++i) qualityTable->setColumnWidth(i,widths[i]);
  qualityTable->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
  if(preferredSignalIndex>=mappings.size()) preferredSignalIndex=0;
  refreshEngineeringTable();registerPlot(form.trendPlot,{},preferredSignalIndex);
  connect(qualityTable,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(row>=0) {preferredSignalIndex=row;if(!plots.empty()) plots.front()->setProperty("signalIndex",row);backend.acquisition.setTraceSignals({unsigned(row)});refreshDataViews();}});
  connect(qualityTable,&QTableWidget::cellDoubleClicked,this,[this](int row,int){preferredSignalIndex=row;plotRawColumn=-1;navigate(7);});
}


void Window::plotPage() {
  auto form=showForm<Ui::PlotsPage>();
  form.plotSource->addItems({"External · CH1 / A","External · CH1 / B","Internal · CH2 / A","Internal · CH2 / B"});form.plotSource->setCurrentIndex((analysisChannel-1)*2+analysisBus);
  connect(form.plotSource,&QComboBox::currentIndexChanged,this,[this](int source){analysisChannel=source/2+1;analysisBus=source%2;for(auto plot:plots) {plot->setProperty("sourceChannel",analysisChannel);plot->setProperty("sourceBus",analysisBus);}refreshDataViews();});
  const auto mappings=backend.mappings();
  if(plotRawColumn>=0) form.plotSignal->addItem("Raw · "+model.headerData(plotRawColumn,Qt::Horizontal,Qt::DisplayRole).toString(),-1);
  for(unsigned i=0;i<mappings.size();++i) form.plotSignal->addItem(QString::fromStdString(mappings[i].id),int(i));
  if(preferredSignalIndex>=mappings.size()) preferredSignalIndex=0;
  form.plotSignal->setCurrentIndex(plotRawColumn>=0 ? 0 : int(preferredSignalIndex));
  connect(form.plotSignal,&QComboBox::currentIndexChanged,this,[this,combo=form.plotSignal](int){const auto signal=combo->currentData().toInt();if(signal>=0) {preferredSignalIndex=signal;plotRawColumn=-1;QTimer::singleShot(0,this,[this]{navigate(7);});}});
  form.pausePlotsButton->setText(plotPaused ? "Resume plots" : "Pause plots");
  connect(form.pausePlotsButton,&QPushButton::clicked,this,[this,button=form.pausePlotsButton] {
    plotPaused=!plotPaused; button->setText(plotPaused ? "Resume plots" : "Pause plots");
    if (!plotPaused) refreshDataViews();
  });
  connect(form.exportPlotButton,&QPushButton::clicked,this,[this] { exportPlot(); });
  cursorALabel=form.cursorA; cursorBLabel=form.cursorB; cursorDeltaLabel=form.cursorDelta;
  connect(form.cursorInspectButton,&QPushButton::clicked,this,[this] { navigate(5); });
  const QList<Plot *> initial={form.signalPlot0,form.signalPlot1,form.signalPlot2,form.signalPlot3};
  for(int i=0;i<initial.size();++i) {
    const auto signal=mappings.empty() ? 0 : (preferredSignalIndex+i)%mappings.size();
    if(auto caption=content->findChild<QLabel *>(QString("plotCard%1Title").arg(i))) caption->setText(i==0 && plotRawColumn>=0 ? model.headerData(plotRawColumn,Qt::Horizontal,Qt::DisplayRole).toString()+" · raw numeric value" : signal<mappings.size() ? QString::fromStdString(mappings[signal].id)+" ("+QString::fromStdString(mappings[signal].unit)+")" : "Import an ICD to plot engineering values");
    if(auto card=content->findChild<QFrame *>(QString("plotCard%1").arg(i))) card->setVisible(i==0 || (plotRawColumn<0 && i<int(mappings.size())));
    registerPlot(initial[i],i==0 && plotRawColumn>=0 ? "raw_column" : "",signal);initial[i]->setProperty("rawColumn",plotRawColumn);configureCursor(initial[i]);
  }
  auto extra = form.extraPlotsLayout;
  auto addTrace=[this,extra](const QString &name) {
    const auto mappings=backend.mappings();const auto it=std::find_if(mappings.begin(),mappings.end(),[&](const auto &s){return QString::fromStdString(s.id)==name;});if(it==mappings.end() || plots.size()>=8)return;
    auto card=new QWidget;Ui::TraceCard trace;trace.setupUi(card);trace.traceTitle->setText(name);extra->addWidget(card);registerPlot(trace.tracePlot,{},unsigned(it-mappings.begin()));configureCursor(trace.tracePlot);
  };
  for(const auto &id:extraPlotIds)addTrace(id);
  connect(form.addSignalButton,&QPushButton::clicked,this,[this,addTrace] {
    if (plots.size()>=8) { toast("Eight trace limit reached."); return; }
    QStringList options; for(const auto &mapping:backend.mappings()) options << QString::fromStdString(mapping.id);
    if(options.empty()) {toast("Import an ICD profile first.");return;}
    bool accepted=false;
    const auto name=QInputDialog::getItem(this,"Add trace","Signal (maximum 8 traces)",options,0,false,&accepted);
    if(!accepted || name.isEmpty()) return;
    // Added traces reuse a Designer-backed plot card; the eight-trace limit is shared.
    extraPlotIds.append(name);addTrace(name);
    refreshDataViews();
  });
}


void Window::exportPlot(){
    auto path = QFileDialog::getSaveFileName(
        this, "Export plot", sessionDir + "/plot.png", "PNG (*.png)");
    if (path.isEmpty())
      return;
    if (content->grab().save(path))
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
  form.rulesTitle->setText("Display filters");form.rulesNotice->setText("Configure signal limits and missing timers in Tasks and automation (Ctrl+Shift+T). Recent 512 events appear here; the complete capture log is stored as .checks.jsonl.");
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
