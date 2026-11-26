#include "main_window.hpp"
#include "services/operator_style.hpp"
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

qsizetype Window::countBus(int bus){
    const auto metrics=backend.acquisition.metrics(false);return metrics.sourceCounts[bus]+metrics.sourceCounts[2+bus];
  }

QString Window::fileSize(QString p){
    return p.isEmpty()
               ? "0 B"
               : QString::number(QFileInfo(p).size() / 1024., 'f', 1) + " KB";
  }





void Window::setMonitorFocus(bool enabled) {
  if(monitorFocused==enabled)return;
  monitorFocused=enabled;
  if(enabled) {
    sidebarBeforeFocus=sidebarCollapsed;
    setSidebarCollapsed(true);
    settings.setValue("sidebarCollapsed",sidebarBeforeFocus);
  } else setSidebarCollapsed(sidebarBeforeFocus);
}

void Window::liveMonitor() {
  if(!viewingReplayStatistics)backend.replaySource=false;
  if(!settings.contains("monitorRowMode")){settings.setValue("monitorRowMode","statistics");settings.setValue("monitorLayout","windows");}
  auto form = showForm<Ui::MonitorPage>();
  if(width()<1550) {
    form.bodyLayout->setSpacing(7);
    form.selectionLayout->setContentsMargins(12,5,12,5);
  }
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
  // Keep connection failures visible after returning to the monitor without
  // restoring the retired full recording-setup card. The panel is hidden
  // during healthy capture and only expands for actionable hardware feedback.
  auto connectionPanel=connectionResultPanel(false);
  form.bodyLayout->insertWidget(0,connectionPanel);
  recordButton=new QPushButton("Start recording");recordButton->setObjectName("startRecordingButton");
  recordButton->setProperty("primary",true);
  form.bodyLayout->insertWidget(0,recordButton);connect(recordButton,&QPushButton::clicked,this,[this]{if(backend.acquisition.active())stopAndSave();else startCapture();});
  auto focus=new QPushButton("Focus");
  focus->setObjectName("monitorFocusMode");
  auto charts=new QPushButton("Plots");
  charts->setObjectName("monitorFocusPlots");
  desktop::actionIcon(charts,"chart");
  form.actionBarLayout->addWidget(charts);
  form.actionBarLayout->addWidget(focus);
  auto layoutMode=new QComboBox;
  layoutMode->setObjectName("workspaceLayout");
  layoutMode->setAccessibleName("Monitor layout");
  layoutMode->addItem("Movable windows","windows");
  layoutMode->addItem("Tabs","tabs");
  layoutMode->setCurrentIndex(settings.value("monitorLayout","windows").toString()=="tabs"?1:0);
  auto arrange=new QPushButton("Tile / Cascade");
  arrange->setObjectName("arrangeMonitorWindows");
  desktop::actionIcon(arrange,"grid",false,15);
  auto arrangeMenu=new QMenu(arrange);
  auto tileAct=arrangeMenu->addAction(desktop::operatorIcon("grid"),"Tile");
  auto cascadeAct=arrangeMenu->addAction(desktop::operatorIcon("layers"),"Cascade");
  arrange->setMenu(arrangeMenu);
  form.actionBarLayout->addWidget(layoutMode);
  form.actionBarLayout->addWidget(arrange);
  const auto updateLayoutActions=[layoutMode,arrange] {
    const bool windows=layoutMode->currentData().toString()=="windows";
    arrange->setVisible(windows);
    arrange->setEnabled(windows);
  };
  updateLayoutActions();
  connect(layoutMode,&QComboBox::currentIndexChanged,this,[this,layoutMode](int){
    persistMonitorWorkspace();settings.setValue("monitorLayout",layoutMode->currentData());applyMonitorLayout();
  });
  connect(layoutMode,&QComboBox::currentIndexChanged,this,[updateLayoutActions](int){updateLayoutActions();});
  connect(tileAct,&QAction::triggered,this,[this]{
    if(monitorWorkspace){for(auto w:channelWindows)w->showNormal();monitorWorkspace->tileSubWindows();}
  });
  connect(cascadeAct,&QAction::triggered,this,[this]{
    if(monitorWorkspace){for(auto w:channelWindows)w->showNormal();monitorWorkspace->cascadeSubWindows();}
  });
  const auto resizeContent=[=,this] {
    form.deviceReadiness->setVisible(!monitorFocused);
    charts->setVisible(monitorFocused);
    const bool windows=layoutMode->currentData().toString()=="windows";
    layoutMode->setVisible(true);arrange->setVisible(windows);
    focus->setText(monitorFocused?"Exit":"Focus");
    desktop::actionIcon(focus,monitorFocused?"collapse":"expand");
  };
  connect(focus,&QPushButton::clicked,focus,[=,this]{
    setMonitorFocus(!monitorFocused);
    resizeContent();refreshOperatorState();
  });
  connect(charts,&QPushButton::clicked,this,&Window::liveEngineering);
  resizeContent();
  desktop::actionIcon(recordButton,"record",true);
  desktop::actionIcon(form.columnsButton,"columns");
  desktop::actionIcon(form.allBusButton,"layers",false,15);
  desktop::actionIcon(form.busAButton,"bus",false,15);
  desktop::actionIcon(form.busBButton,"bus",false,15);
  desktop::actionIcon(form.inspectButton,"search");
  desktop::actionIcon(form.engineeringButton,"sliders");
  desktop::actionIcon(form.plotButton,"chart");
  form.hexSearch->addAction(desktop::operatorIcon("search"),QLineEdit::LeadingPosition);
  form.hexSearch->setPlaceholderText("Search hex words…");
  for(int index=0;index<2;++index) {
    auto frame=index?form.deviceState1:form.deviceState0;
    auto column=qobject_cast<QVBoxLayout *>(frame->layout());
    auto icon=new QLabel(frame);
    icon->setProperty("deviceIcon",index?"internal":"external");
    desktop::labelIcon(icon,"device",25);
    auto text=new QVBoxLayout;
    text->setContentsMargins(0,0,0,0);
    text->setSpacing(3);
    text->addWidget(deviceTitles[index]);
    text->addWidget(deviceStatuses[index]);
    auto badge=new QLabel(frame);
    badge->setProperty("channelBadge",true);
    badge->setObjectName(QString("deviceBadge%1").arg(index));
    auto row=new QHBoxLayout;
    row->addWidget(icon,0,Qt::AlignVCenter);
    row->addLayout(text,1);
    row->addWidget(badge,0,Qt::AlignTop);
    column->addLayout(row);
  }
  form.inspectButton->setText("Details");
  form.inspectButton->show();
  connect(form.inspectButton,&QPushButton::clicked,this,[this]{
    if(const auto r=selectedMonitorRecord())openRecordDetailsDialog(*r);
    else if(!backend.selected.words.empty())openRecordDetailsDialog(backend.selected);
    else toast("Select a captured record first to inspect words and signals.");
  });
  if(auto mapBtn=content->findChild<QPushButton *>("mapRouteButton")) {
    desktop::actionIcon(mapBtn,"edit");
    connect(mapBtn,&QPushButton::clicked,this,[this]{
      const aero::Record *r=selectedMonitorRecord();
      aero::Record fallback;
      if(!r && !backend.selected.words.empty()) r=&backend.selected;
      if(!r){toast("Select a captured record first to pre-fill Signal Mappings.");return;}
      const auto p=aero::interpret(*r);
      const auto cmd=p.payloadCommand.count ? p.payloadCommand : aero::command(r->commandWord);
      PreFillMappingRoute route;route.valid=true;
      route.rt=cmd.rt;route.sa=cmd.sa;route.tx=cmd.transmit;
      route.wordCount=cmd.count ? int(cmd.count) : 32;
      route.channel=int(r->channel);route.bus=r->bus;
      for(size_t i=0;i<r->words.size()&&i<32;++i)route.sampleWords[i]=r->words[i];
      route.hasSample=!r->words.empty();
      openSignalMappingForRoute(route);
    });
  }

  auto postBanner=new QFrame(content);
  postBanner->setObjectName("postSaveBanner");
  auto bannerLay=new QHBoxLayout(postBanner);
  bannerLay->setContentsMargins(12,8,12,8);bannerLay->setSpacing(8);
  auto bannerText=new QLabel("Recording finalized and CSVs saved. Next step:",postBanner);
  bannerText->setStyleSheet("font-weight:600;color:#12593a;");
  auto bannerCsv=new QPushButton("Open CSV Viewer",postBanner);desktop::actionIcon(bannerCsv,"file");
  auto bannerMap=new QPushButton("Map Signals →",postBanner);desktop::actionIcon(bannerMap,"edit");
  auto bannerExp=new QPushButton("Exports & Reports",postBanner);desktop::actionIcon(bannerExp,"download");
  auto bannerHide=new QPushButton("Dismiss",postBanner);
  bannerLay->addWidget(bannerText,1);bannerLay->addWidget(bannerCsv);bannerLay->addWidget(bannerMap);bannerLay->addWidget(bannerExp);bannerLay->addWidget(bannerHide);
  postBanner->hide();
  form.bodyLayout->insertWidget(1,postBanner);
  auto bannerDismissed=std::make_shared<bool>(false);
  connect(bannerCsv,&QPushButton::clicked,this,[this]{csvTools(currentFile);});
  connect(bannerMap,&QPushButton::clicked,this,[this]{navigate(12);});
  connect(bannerExp,&QPushButton::clicked,this,[this]{navigate(14);});
  connect(bannerHide,&QPushButton::clicked,postBanner,[postBanner,bannerDismissed]{*bannerDismissed=true;postBanner->hide();});
  connect(&tick,&QTimer::timeout,postBanner,[this,postBanner,bannerDismissed]{
    if(backend.acquisition.active()) *bannerDismissed=false;
    const bool ready=!*bannerDismissed && !savingSession && sessionSaveState=="Complete" && !currentFile.isEmpty();
    if(postBanner->isVisible()!=ready) postBanner->setVisible(ready);
  });
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
  form.engineeringButton->setText("Live Engineering");
  connect(form.engineeringButton,&QPushButton::clicked,this,&Window::liveEngineering);
  connect(form.plotButton,&QPushButton::clicked,this,[this]{plotMonitorSelection();});
  connect(monitor->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](const QModelIndex &index){
    if(const auto record=model.at(index.row())) {selectedMonitorId=record->id;selectedMonitorColumn=index.column();backend.selected=*record;}
    refreshOperatorState();
  });
  (model.busFilter=="A" ? form.busAButton : model.busFilter=="B" ? form.busBButton : form.allBusButton)->setChecked(true);
  connect(monitor, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {if(index.column()>=17) plotMonitorSelection();});
  if (model.rowCount()) monitor->selectRow(0);
  monitor->horizontalHeader()->restoreState(settings.value("monitorHeader").toByteArray());
  buildMonitorPanes(form.monitor);
  form.bodyLayout->setStretchFactor(form.monitor,1);
  auto empty=new desktop::TableNotice(form.monitor,"monitor-empty",60);
  empty->setObjectName("emptyMonitorNotice");
  const auto refreshEmpty=[this,empty,form] {
    const bool hasRecords=!model.records.empty()||backend.acquisition.active()||viewingReplayStatistics;
    form.monitorStatus->setVisible(hasRecords);
    form.selectedSummary->parentWidget()->setVisible(hasRecords&&!monitorFocused);
    if(model.rowCount()!=0) {
      empty->showState(false,{},{});
      return;
    }
    const bool active=backend.acquisition.active();
    const bool filtered=!model.busFilter.isEmpty()||model.rtFilter>=0||!model.search.isEmpty();
    const bool connected=!backend.captureConfig.hardware||backend.acquisition.connected();
    const auto title=active?(filtered?"No matching records":"Waiting for bus traffic"):
      connected?"Ready to record":"Connect a device to record";
    const auto detail=active?(filtered?"Change the bus, RT, or search filter.":"Incoming records will appear here."):
      connected?"Click Start recording to begin capturing bus traffic.":
                "Connect devices, then click Start recording.";
    empty->showState(true,title,detail);
  };
  connect(&model,&QAbstractItemModel::modelReset,empty,refreshEmpty);
  connect(&tick,&QTimer::timeout,empty,refreshEmpty);
  refreshEmpty();
  form.csvButton->hide();
}


void Window::openRecordDetailsDialog(const aero::Record &record) {
  backend.selected = record;
  auto dialog = new QDialog(this);
  dialog->setObjectName("recordDetailsDialog");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(record.id ? QString("Frame #%1 Details").arg(record.id) : QString("Frame Details"));
  const auto available = screen()->availableGeometry();
  dialog->resize(qMin(960, available.width() - 48), qMin(680, available.height() - 60));

  auto layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(12);

  const auto command = aero::command(record.commandWord);
  const auto protocol = aero::interpret(record);
  auto summaryCard = new QFrame(dialog);
  summaryCard->setProperty("card", true);
  auto summaryGrid = new QGridLayout(summaryCard);
  summaryGrid->setContentsMargins(14, 12, 14, 12);
  summaryGrid->setHorizontalSpacing(18);
  summaryGrid->setVerticalSpacing(6);
  auto addSummary = [&](int r, int c, const QString &k, const QString &v, const QString &tip = {}) {
    auto key = new QLabel(k, summaryCard);
    key->setProperty("tone", "muted");
    auto val = new QLabel(v, summaryCard);
    val->setStyleSheet("font-weight:600;");
    if (!tip.isEmpty()) val->setToolTip(tip);
    summaryGrid->addWidget(key, r, c * 2);
    summaryGrid->addWidget(val, r, c * 2 + 1);
  };
  const QString integrityText = !record.id ? "No frame"
    : !protocol.supported ? "Unsupported / insufficient evidence"
    : !protocol.valid ? "Review message structure"
    : (protocol.terminalBusy || protocol.terminalFault || protocol.terminalMessageError) ? "Received · RT status flag set"
    : "Structure checks passed";
  addSummary(0, 0, "Frame / Timestamp", QString("#%1 · %2").arg(record.id).arg(stamp(record.timestampNs)));
  addSummary(0, 1, "Source", QString("Channel %1 · Bus %2").arg(record.channel).arg(record.bus ? "B" : "A"));
  addSummary(1, 0, "Command Word", QString("0x%1 (RT %2 · SA %3 · %4 · %5 %6)")
    .arg(hexword(record.commandWord)).arg(command.rt).arg(command.sa)
    .arg(command.transmit ? "Tx" : "Rx").arg(command.mode ? "Mode" : "WC").arg(command.count));
  addSummary(1, 1, "Status / Flags", QString("0x%1 · Flags %2 · %3")
    .arg(hexword(record.statusWord)).arg(record.flags).arg(integrityText),
    QString::fromStdString(aero::protocolDiagnostics(record, protocol)));
  layout->addWidget(summaryCard);

  auto split = new QSplitter(Qt::Horizontal, dialog);
  auto rawGroup = new QWidget(split);
  auto rawLayout = new QVBoxLayout(rawGroup);
  rawLayout->setContentsMargins(0, 0, 0, 0);
  auto rawTitle = new QLabel("0-based Data Words (Word 0 to Word 31)", rawGroup);
  rawTitle->setProperty("heading", true);
  rawLayout->addWidget(rawTitle);
  auto rawTable = new QTableWidget(0, 4, rawGroup);
  rawTable->setHorizontalHeaderLabels({"Word", "Hex", "Unsigned", "Binary (16-bit)"});
  rawTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  rawTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  rawTable->setAlternatingRowColors(true);
  rawTable->verticalHeader()->hide();
  QList<QStringList> rows;
  for (int i = 0; i < int(record.words.size()); ++i)
    rows << QStringList{QString("Word %1").arg(i), "0x" + hexword(record.words[i]),
      QString::number(record.words[i]), QString::number(record.words[i], 2).rightJustified(16, '0')};
  populate(rawTable, rows);
  rawTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  rawTable->horizontalHeader()->setStretchLastSection(true);
  rawLayout->addWidget(rawTable, 1);

  auto engGroup = new QWidget(split);
  auto engLayout = new QVBoxLayout(engGroup);
  engLayout->setContentsMargins(0, 0, 0, 0);
  auto engTitle = new QLabel("Matching Active ICD Signals", engGroup);
  engTitle->setProperty("heading", true);
  engLayout->addWidget(engTitle);
  auto engTable = new QTableWidget(0, 4, engGroup);
  engTable->setHorizontalHeaderLabels({"Signal", "Engineering Value", "Unit", "Quality"});
  engTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  engTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  engTable->setAlternatingRowColors(true);
  engTable->verticalHeader()->hide();
  QList<QStringList> decodedRows;
  for (const auto &sig : backend.mappings()) {
    if (!aero::matchesSignalRoute(record, sig, aero::signalCommand(protocol, sig))) continue;
    if (sig.sourceWord == aero::SignalWord::Payload && sig.expectedWordCount &&
        (protocol.mode || protocol.payloadCommand.count != sig.expectedWordCount)) continue;
    const auto decoded = aero::decodeValue(record, sig, protocol, backend.exampleContext());
    const auto name = QString::fromStdString(sig.sourceSignalId.empty() ? sig.id : sig.sourceSignalId);
    decodedRows << QStringList{name, decoded.value ? QString::number(*decoded.value, 'g', 12) : "—",
      QString::fromStdString(sig.unit), decoded.value ? "VALID" : QString::fromUtf8(decoded.reason.data(), qsizetype(decoded.reason.size()))};
  }
  populate(engTable, decodedRows);
  engTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  engTable->horizontalHeader()->setStretchLastSection(true);
  engLayout->addWidget(engTable, 1);
  split->setSizes({470, 450});
  layout->addWidget(split, 1);

  auto footer = new QHBoxLayout;
  auto mapBtn = new QPushButton("Map this RT/SA in ICD →", dialog);
  mapBtn->setProperty("primary", true);
  desktop::actionIcon(mapBtn, "edit", true);
  auto engBtn = new QPushButton("Open Engineering", dialog);
  auto closeBtn = new QPushButton("Close", dialog);
  footer->addWidget(mapBtn);
  footer->addWidget(engBtn);
  footer->addStretch();
  footer->addWidget(closeBtn);
  layout->addLayout(footer);

  connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
  connect(engBtn, &QPushButton::clicked, dialog, [this, dialog] { dialog->accept(); navigate(6); });
  connect(mapBtn, &QPushButton::clicked, dialog, [this, dialog, record, protocol] {
    PreFillMappingRoute route;
    route.valid = true;
    const auto cmd = protocol.payloadCommand.count ? protocol.payloadCommand : aero::command(record.commandWord);
    route.rt = cmd.rt;
    route.sa = cmd.sa;
    route.tx = cmd.transmit;
    route.wordCount = cmd.count ? int(cmd.count) : 32;
    route.channel = int(record.channel);
    route.bus = record.bus;
    for (size_t i = 0; i < record.words.size() && i < 32; ++i) route.sampleWords[i] = record.words[i];
    route.hasSample = !record.words.empty();
    dialog->accept();
    openSignalMappingForRoute(route);
  });
  dialog->show();
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
    const QStringList liveCells={raw.isEmpty()?QString("—"):raw.join(' '),value.value ? QString::number(*value.value,'g',12)+(mappings[row].unit.empty()?QString():" "+QString::fromStdString(mappings[row].unit)) : "—",value.value ? QString("VALID · %1").arg(stamp(value.timestampNs)) : value.matched ? QString::fromUtf8(value.reason.data(),qsizetype(value.reason.size())) : trafficMismatch(mappings[row],analysisChannel,analysisBus)};
    for(int offset=0;offset<liveCells.size() && 5+offset<qualityTable->columnCount();++offset) {
      const int column=5+offset;
      auto item=qualityTable->item(row,column);if(!item) {item=new QTableWidgetItem;qualityTable->setItem(row,column,item);}
      if(item->text()!=liveCells[offset]) {item->setText(liveCells[offset]);item->setToolTip(liveCells[offset]);}
    }
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
  form.cursorInspectButton->hide();
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
  const auto updateTraces=[this,initial]{std::vector<unsigned> selected;for(auto plot:initial)if(plot->property("role").toString().isEmpty()&&plot->property("signalIndex").toInt()>=0)selected.push_back(plot->property("signalIndex").toUInt());backend.acquisition.setTraceSignals(selected);};
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
      cursorAValue.reset();updateTitle();updateTraces();refreshDataViews();
    });
    connect(expanders[i],&QPushButton::clicked,this,[expand,i]{expand(i);});

  }
  // One compact control row; the chart owns the remaining vertical space.
  auto header=form.actionBar->parentWidget();
  form.bodyLayout->insertWidget(0,form.actionBar);if(header&&header!=form.actionBar->parentWidget())header->hide();
  form.actionBarLayout->setContentsMargins(0,0,0,0);form.actionBarLayout->setSpacing(6);
  form.actionBar->setStyleSheet("QComboBox,QPushButton,QToolButton {padding:5px 8px;min-height:24px;border-radius:7px;}");
  form.panel0Controls->hide();
  form.restorePlotsButton->hide();
  form.pausePlotsButton->hide();
  form.exportPlotButton->hide();
  form.actionBarLayout->insertWidget(1,form.panel0Select,1);
  form.cursorTools->hide();
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
