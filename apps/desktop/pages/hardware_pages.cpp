#include "main_window.hpp"
#include "services/ddc_discovery.hpp"
#include "services/session_store.hpp"
#include "services/runtime_paths.hpp"
#include "ui_rt_channel_body.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif
void Window::rescanDdc(QLabel *target) {
  if(backend.captureConfig.hardware&&(connecting||backend.acquisition.active()||savingSession)){toast("Stop monitoring and finish saving before rescanning the DDC runtime");return;}
  if(discoveryRunning){toast("A DDC scan is already running");return;}
  discoveryRunning=true;QPointer<QLabel> label=target;if(label)label->setText("Inspecting Windows device and runtime evidence…");
  auto watcher=new QFutureWatcher<QJsonObject>(this);
  connect(watcher,&QFutureWatcher<QJsonObject>::finished,this,[this,watcher,label]{hardwareDiscovery=watcher->result();discoveryRunning=false;
    applicationEvent("ddc_discovery","",hardwareDiscovery);if(label)label->setText(desktop::discoverySummary(hardwareDiscovery));
    toast(hardwareDiscovery.value("physical_presence").toString()=="unknown"?"DDC scan incomplete; hardware presence unknown":QString("DDC scan complete: %1 matching Windows device(s). Logical assignments require Card Manager.").arg(hardwareDiscovery.value("detected_devices").toInt()));watcher->deleteLater();});
  const auto path=QString::fromStdWString(backend.captureConfig.runtime.wstring());watcher->setFuture(QtConcurrent::run([path]{return desktop::discoverDdc(path);}));
}
void Window::openCardManager() {
#ifdef Q_OS_WIN
  const auto path=QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath("System32/ddccm.exe");
  if(!QFileInfo::exists(path)){toast("DDC Card Manager is not installed. Install the matching vendor SDK/driver on this PC.");return;}
  const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"runas",reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,nullptr,SW_SHOWNORMAL));
  applicationEvent("ddc_card_manager_launch",path,{{"result",double(result)}});
  if(result<=32)toast("DDC Card Manager did not open; Windows elevation may have been cancelled.");
#else
  toast("DDC Card Manager is available on Windows");
#endif
}
void Window::stopAndSave(QString recording) {
  if(savingSession){toast("Stop & save is already running");return;}
  if(connecting){toast("Wait for connection to finish or cancel it before saving");return;}
  const auto path=!recording.isEmpty()?recording:(backend.acquisition.active()?currentFile:((page==11&&backend.replaySource&&!currentReplay.isEmpty())?currentReplay:currentFile));
  if(!recording.isEmpty()&&recording!=currentFile&&backend.acquisition.active()){toast("Stop the active capture before exporting another session");return;}
  if(path.isEmpty()||!QFileInfo::exists(path)){toast("No recording to save. The DDC connection must succeed first.");return;}
  automaticHeld=true;configurationRetry.stop();savingSession=true;sessionSaveState="Stopping and draining";
  applicationEvent("session_save_requested",path);statusBar()->showMessage("Stopping acquisition and draining pending hardware buffers…");
  auto stopped=new QFutureWatcher<QString>(this);
  connect(stopped,&QFutureWatcher<QString>::finished,this,[this,stopped,path]{const auto error=stopped->result();stopped->deleteLater();
    if(!error.isEmpty()){disconnectAfterSave=false;savingSession=false;toast(error);refreshOperatorState();return;}
    sessionSaveState="Finalized — awaiting export destination";
    QFile descriptor(path+".session.json");if(descriptor.open(QIODevice::ReadOnly)){const auto o=QJsonDocument::fromJson(descriptor.readAll()).object();if(!o.value("export_directory").toString().isEmpty()){
      settings.setValue("exportFolder",o.value("export_directory").toString());settings.setValue("nextRunName",o.value("run_name").toString("run"+o.value("run_number").toString()));
      const auto allocatedFolder=o.value("export_directory").toString(),allocatedName=o.value("run_name").toString("run"+o.value("run_number").toString());if(runNameEdit)runNameEdit->setText(allocatedName);if(csvFolderEdit)csvFolderEdit->setText(allocatedFolder);settings.setValue("exportFolder",allocatedFolder);settings.setValue("nextRunName",allocatedName);}}
    descriptor.close();
    const auto dir=settings.value("exportFolder").toString().trimmed(),name=settings.value("nextRunName").toString().trimmed();
    if((dir.isEmpty()||!desktop::validateRunName(name).isEmpty())&&!chooseSaveDestination()){savingSession=false;if(disconnectAfterSave){disconnectAfterSave=false;backend.acquisition.stop();applicationEvent("devices_disconnected");}sessionSaveState="Recording retained — CSV save cancelled";toast(sessionSaveState);refreshOperatorState();return;}
    exportFinalizedSession(path);
  });
  stopped->setFuture(QtConcurrent::run([this]{try{backend.acquisition.stopRecording();return QString();}catch(const std::exception &e){return QString::fromUtf8(e.what());}}));refreshOperatorState();
}
void Window::exportFinalizedSession(const QString &path) {
  const auto dir=settings.value("exportFolder").toString().trimmed(),name=settings.value("nextRunName").toString().trimmed();
  sessionSaveState="Exporting complete recording";statusBar()->showMessage(sessionSaveState);if(csvFolderEdit)csvFolderEdit->setEnabled(false);if(runNameEdit)runNameEdit->setEnabled(false);
  auto watcher=new QFutureWatcher<desktop::SessionSaveResult>(this);
  connect(watcher,&QFutureWatcher<desktop::SessionSaveResult>::finished,this,[this,watcher,dir]{const auto result=watcher->result();savingSession=false;sessionSaveState=result.error.isEmpty()?"Complete":"Save failed";
    if(csvFolderEdit)csvFolderEdit->setEnabled(true);if(runNameEdit)runNameEdit->setEnabled(true);
    toast(result.error.isEmpty()?QString("%1%2 saved: %3 Internal, %4 External records. CSVs: %5. Replay recording retained.%6")
      .arg(result.incomplete?"INCOMPLETE / recovered: ":"").arg(result.runName).arg(result.internal).arg(result.external).arg(dir).arg(!result.internal||!result.external?" Empty channels have header-only CSVs.":""):result.error+" — recording retained; retry save.");
    if(result.error.isEmpty()){settings.setValue("nextRunName",desktop::suggestedRunName(dir));if(runNameEdit)runNameEdit->setText(settings.value("nextRunName").toString());}
    if(disconnectAfterSave){disconnectAfterSave=false;backend.acquisition.stop();applicationEvent("devices_disconnected");}
    refreshOperatorState();watcher->deleteLater();
  });
  watcher->setFuture(QtConcurrent::run([path,dir,name]{try{return desktop::exportSession(path,dir,{},name);}catch(const std::exception &e){desktop::SessionSaveResult r;r.error=QString::fromUtf8(e.what());return r;}}));refreshOperatorState();
}
const aero::Record *Window::selectedMonitorRecord() const {
  if(page==4&&rtGrouped&&monitorWorkspace&&monitorWorkspace->isVisible()) {
    const auto c=settings.value("activeMonitorChannel",0).toInt()==1?1:0;
    if(auto tree=rtViews[c]){const auto index=tree->currentIndex();if(index.isValid()&&!tree->isRowHidden(index.row(),index.parent()))return rtModels[c].recordFor(index);}
    return nullptr;
  }
  if(!monitor)return nullptr;
  const auto *source=dynamic_cast<const RecordModel *>(monitor->model());
  return source?source->at(monitor->currentIndex().row()):nullptr;
}
void Window::buildMonitorPanes(QTableView *original) {
  tabMonitor=original;
  auto container=original->parentWidget();auto layout=qobject_cast<QVBoxLayout *>(container->layout());
  monitorWorkspace=new QMdiArea(container);monitorWorkspace->setObjectName("monitorWorkspace");monitorWorkspace->setBackground(QColor("#eef1e9"));monitorWorkspace->setMinimumHeight(280);auto workspacePalette=monitorWorkspace->palette();workspacePalette.setColor(QPalette::Highlight,QColor("#315f43"));workspacePalette.setColor(QPalette::HighlightedText,Qt::white);workspacePalette.setColor(QPalette::WindowText,QColor("#2e3931"));monitorWorkspace->setPalette(workspacePalette);
  layout->insertWidget(layout->indexOf(original),monitorWorkspace,1);
  rtGrouped=settings.value("monitorRowMode","rt").toString()=="rt";
  auto rowTools=new QHBoxLayout;auto grouping=new QComboBox;grouping->setObjectName("monitorRowMode");grouping->setAccessibleName("Live monitor row grouping");grouping->addItems({"Grouped by RT","Chronological records"});grouping->setCurrentIndex(rtGrouped?0:1);
  auto pause=new QPushButton("Pause RT display");pause->setObjectName("pauseRtDisplay");rowTools->addWidget(grouping);rowTools->addWidget(pause);rowTools->addStretch();layout->insertLayout(layout->indexOf(monitorWorkspace),rowTools);
  if(recordButton)rowTools->addWidget(recordButton);
  connect(grouping,&QComboBox::currentIndexChanged,container,[this,pause](int mode){rtGrouped=mode==0;settings.setValue("monitorRowMode",rtGrouped?"rt":"raw");pause->setEnabled(rtGrouped);refreshMonitorPanes();});
  connect(pause,&QPushButton::clicked,container,[this,pause]{rtDisplayPaused=!rtDisplayPaused;pause->setText(rtDisplayPaused?"Resume RT display":"Pause RT display");refreshRtMonitor();});
  pause->setEnabled(rtGrouped);
  for(const auto channel:{0,1}) {
    auto &source=paneModels[channel];source.channelFilter=channel+1;source.sharedSource=&model;source.refresh();
    if(!paneSelection[channel])paneSelection[channel]=settings.value(QString("pane%1Selection").arg(channel)).toULongLong();
    source.busFilter=settings.value(QString("pane%1Bus").arg(channel)).toString();source.rtFilter=settings.value(QString("pane%1Rt").arg(channel),-1).toInt();source.search=settings.value(QString("pane%1Search").arg(channel)).toString();
    auto panel=new QFrame;panel->setProperty("channelRole",channel?"internal":"external");auto body=new QVBoxLayout(panel);body->setContentsMargins(0,0,0,0);body->setSpacing(4);
    auto tools=new QHBoxLayout;auto count=new QLabel;count->setWordWrap(true);count->setMinimumWidth(80);paneCounts[channel]=count;body->addWidget(count);
    auto bus=new QComboBox;bus->setObjectName(QString("pane%1BusFilter").arg(channel));bus->addItems({"All buses","Bus A","Bus B"});bus->setCurrentIndex(source.busFilter=="A"?1:source.busFilter=="B"?2:0);bus->setAccessibleName(channel?"Internal bus filter":"External bus filter");
    auto rt=new QComboBox;rt->addItem("All RTs");for(int n=0;n<32;++n)rt->addItem("RT "+QString::number(n));rt->setCurrentIndex(source.rtFilter+1);rt->setAccessibleName("RT filter");
    auto search=new QLineEdit(source.search);search->setPlaceholderText("Hex search");search->setMaximumWidth(180);search->setMinimumWidth(65);search->setAccessibleName("Hex word filter");
    auto columns=new QPushButton("Raw columns");columns->setProperty("rawColumnsOnly",true);columns->setEnabled(!rtGrouped);tools->addWidget(bus);tools->addWidget(rt);tools->addWidget(search);tools->addWidget(columns);body->addLayout(tools);
    auto view=new QTableView(panel);paneViews[channel]=view;view->setObjectName(channel?"internalMonitor":"externalMonitor");view->setAccessibleName(channel?"Internal traffic":"External traffic");view->setModel(&source);view->setSelectionBehavior(QAbstractItemView::SelectRows);view->setSelectionMode(QAbstractItemView::SingleSelection);view->setAlternatingRowColors(true);view->setEditTriggers(QAbstractItemView::NoEditTriggers);view->verticalHeader()->hide();view->setMinimumHeight(90);view->verticalHeader()->setDefaultSectionSize(settings.value("dense",false).toBool()?26:32);body->addWidget(view,1);
    auto rtPanel=new QWidget(panel);Ui::RtChannelBody rtForm;rtForm.setupUi(rtPanel);body->addWidget(rtPanel,1);rtViews[channel]=rtForm.rtTree;
    rtForm.rtTree->setObjectName(channel?"internalRtMonitor":"externalRtMonitor");rtForm.rtTree->setAccessibleName(channel?"Internal RT activity":"External RT activity");rtForm.rtTree->setModel(&rtModels[channel]);rtForm.rtTree->setMinimumHeight(90);
    rtForm.rtTree->header()->setSectionResizeMode(QHeaderView::Interactive);rtForm.rtTree->setIndentation(14);
    connect(rtForm.rtTree->selectionModel(),&QItemSelectionModel::currentChanged,panel,[this,channel](const QModelIndex &i){if(const auto record=rtModels[channel].recordFor(i)){settings.setValue("activeMonitorChannel",channel);backend.selected=*record;selectedMonitorId=record->id;refreshOperatorState();}});
    connect(rtForm.rtTree,&QTreeView::clicked,panel,[this,channel](const QModelIndex &i){if(const auto record=rtModels[channel].recordFor(i)){settings.setValue("activeMonitorChannel",channel);backend.selected=*record;selectedMonitorId=record->id;refreshOperatorState();}});
    connect(rtForm.rtTree,&QTreeView::doubleClicked,panel,[this,channel](const QModelIndex &i){if(const auto record=rtModels[channel].recordFor(i)){settings.setValue("activeMonitorChannel",channel);backend.selected=*record;inspectSelection();}});
    const QList<int> widths{42,145,65,38,36,36,72,42,220,80,70};
    auto menu=new QMenu(columns);columns->setMenu(menu);
    for(int i=0;i<source.columnCount();++i){const auto key=QString("pane%1Column%2").arg(channel).arg(i);view->setColumnWidth(i,i<widths.size()?widths[i]:100);view->setColumnHidden(i,!settings.value(key,i<11&&i!=2&&i!=7&&i!=9).toBool());auto action=menu->addAction(source.headerData(i,Qt::Horizontal,Qt::DisplayRole).toString());action->setCheckable(true);action->setChecked(!view->isColumnHidden(i));connect(action,&QAction::toggled,view,[this,view,key,i](bool on){view->setColumnHidden(i,!on);settings.setValue(key,on);});}
    view->horizontalHeader()->setSectionResizeMode(8,QHeaderView::Stretch);
    connect(bus,&QComboBox::currentIndexChanged,panel,[this,channel](int i){paneModels[channel].busFilter=i==1?"A":i==2?"B":"";settings.setValue(QString("pane%1Bus").arg(channel),paneModels[channel].busFilter);refreshMonitorPanes();});
    connect(rt,&QComboBox::currentIndexChanged,panel,[this,channel](int i){paneModels[channel].rtFilter=i-1;settings.setValue(QString("pane%1Rt").arg(channel),i-1);refreshMonitorPanes();});
    connect(search,&QLineEdit::textChanged,panel,[this,channel](const QString &v){paneModels[channel].search=v;settings.setValue(QString("pane%1Search").arg(channel),v);refreshMonitorPanes();});
    connect(view->selectionModel(),&QItemSelectionModel::currentChanged,panel,[this,view,channel](const QModelIndex &i){
      if(const auto r=paneModels[channel].at(i.row())){monitor=view;paneSelection[channel]=r->id;paneColumns[channel]=i.column();backend.selected=*r;selectedMonitorId=r->id;selectedMonitorColumn=i.column();settings.setValue("activeMonitorChannel",channel);refreshOperatorState();}});
    connect(view,&QTableView::clicked,panel,[this,view,channel](const QModelIndex &i){if(const auto r=paneModels[channel].at(i.row())){monitor=view;backend.selected=*r;selectedMonitorId=r->id;selectedMonitorColumn=i.column();settings.setValue("activeMonitorChannel",channel);refreshOperatorState();}});
    connect(view,&QTableView::doubleClicked,panel,[this,view](const QModelIndex &i){monitor=view;if(i.column()>=17)plotMonitorSelection();else inspectSelection();});
    auto window=monitorWorkspace->addSubWindow(panel,Qt::SubWindow|Qt::CustomizeWindowHint|Qt::WindowTitleHint|Qt::WindowMinMaxButtonsHint);
    // Child windows stay inside the workspace; neither window can stop capture.
    channelWindows[channel]=window;window->setWindowTitle(QString("%1 / Channel %2 / Logical %3").arg(channel?"Internal":"External").arg(channel+1).arg(backend.captureConfig.logicalDevices[channel]));window->setMinimumSize(320,220);window->show();
  }
  connect(monitorWorkspace,&QMdiArea::subWindowActivated,this,[this](QMdiSubWindow *w){for(int c=0;c<2;++c)if(w==channelWindows[c]){monitor=paneViews[c];settings.setValue("activeMonitorChannel",c);}});
  applyMonitorLayout();refreshMonitorPanes();
  QTimer::singleShot(0,monitorWorkspace,[this]{
    if(!monitorWorkspace)return;const auto size=monitorWorkspace->viewport()->size();
    for(int c=0;c<2;++c)if(auto w=channelWindows[c]) {
      const auto key=QString("monitorWindow%1").arg(c);auto rect=settings.value(key).toRect();
      if(!rect.isValid())rect=QRect(c*(size.width()/2),0,qMax(320,size.width()/2),qMax(240,size.height()));
      rect.setWidth(qMin(rect.width(),qMax(320,size.width())));rect.setHeight(qMin(rect.height(),qMax(220,size.height())));rect.moveLeft(qBound(0,rect.left(),qMax(0,size.width()-rect.width())));rect.moveTop(qBound(0,rect.top(),qMax(0,size.height()-rect.height())));w->setGeometry(rect);
      auto header=paneViews[c]->horizontalHeader();header->restoreState(settings.value(QString("pane%1Header").arg(c)).toByteArray());
      if(!settings.value("autoscroll",true).toBool())paneViews[c]->verticalScrollBar()->setValue(settings.value(QString("pane%1Scroll").arg(c)).toInt());
    }
  });
}
void Window::persistMonitorWorkspace() {
  for(int c=0;c<2;++c)if(auto w=channelWindows[c]){settings.setValue(QString("monitorWindow%1").arg(c),w->isMaximized()?w->normalGeometry():w->geometry());settings.setValue(QString("pane%1Header").arg(c),paneViews[c]->horizontalHeader()->saveState());settings.setValue(QString("pane%1Selection").arg(c),QVariant::fromValue<qulonglong>(paneSelection[c]));settings.setValue(QString("pane%1Scroll").arg(c),paneViews[c]->verticalScrollBar()->value());}
  if(tabMonitor)settings.setValue("monitorHeader",tabMonitor->horizontalHeader()->saveState());
}
void Window::applyMonitorLayout() {
  const bool windows=settings.value("monitorLayout","windows").toString()!="tabs";
  monitorWorkspace->setVisible(windows);tabMonitor->setVisible(!windows);deviceTabs->setVisible(!windows);
  if(auto readiness=content->findChild<QWidget *>("deviceReadiness"))readiness->setVisible(!windows);
  for(const auto &name:{"allBusButton","busAButton","busBButton","rtFilter","hexSearch","columnsButton"})if(auto w=content->findChild<QWidget *>(name))w->setVisible(!windows);
  monitor=windows?paneViews[settings.value("activeMonitorChannel",0).toInt()==1?1:0]:tabMonitor;
  if(auto grouping=content->findChild<QComboBox *>("monitorRowMode")){QSignalBlocker block(grouping);grouping->setEnabled(windows);grouping->setCurrentIndex(windows&&rtGrouped?0:1);}
  if(auto pause=content->findChild<QPushButton *>("pauseRtDisplay"))pause->setEnabled(windows&&rtGrouped);
  refreshOperatorState();
}
void Window::refreshMonitorPanes(bool incremental) {
  refreshRtMonitor();
  for(int c=0;c<2;++c)if(auto view=paneViews[c]) {
    QSignalBlocker selectionGuard(view->selectionModel());
    auto &source=paneModels[c];if(incremental)source.syncAppend();else source.refresh();
    for(int i=0;i<source.rowCount();++i)if(source.at(i)->id==paneSelection[c]){view->selectionModel()->setCurrentIndex(source.index(i,paneColumns[c]),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);break;}
    if(settings.value("autoscroll",true).toBool())view->scrollToBottom();
    const auto m=backend.acquisition.metrics(false);const auto count=m.sourceCounts[c*2]+m.sourceCounts[c*2+1];
    if(paneCounts[c]) {
      const auto phase=m.devices[c].phase;
      const auto state=phase==aero::DevicePhase::Monitoring?(count?"Receiving":"Monitoring / no traffic"):phase==aero::DevicePhase::Simulating?"Simulation":phase==aero::DevicePhase::Failed?"Connection failed":phase==aero::DevicePhase::Disabled?"Disabled":phase==aero::DevicePhase::Checking?"Checking":phase==aero::DevicePhase::Ready?"Connected · waiting for Start recording":"Stopped";
      paneCounts[c]->setText(QString("%1 · CH%2 / logical %3 · %4\n%5 recorded · %6 shown").arg(c?"Internal":"External").arg(c+1).arg(backend.captureConfig.logicalDevices[c]).arg(state).arg(count).arg(source.rowCount()));
      if(rtGrouped)paneCounts[c]->setText(QString("%1 · CH%2 / logical %3 · %4\nA: %5 messages · B: %6 messages · %7 RT groups%8").arg(c?"Internal":"External").arg(c+1).arg(backend.captureConfig.logicalDevices[c]).arg(state).arg(m.sourceCounts[c*2]).arg(m.sourceCounts[c*2+1]).arg(rtModels[c].rowCount()).arg(rtDisplayPaused?" · DISPLAY PAUSED":""));
      paneCounts[c]->setToolTip(QString::fromStdString(m.devices[c].message));
    }
  }
}
void Window::refreshRtMonitor() {
  for(auto button:content->findChildren<QPushButton *>())if(button->property("rawColumnsOnly").toBool())button->setEnabled(!rtGrouped);
  auto frame=backend.acquisition.rtFrame();aero::RtMonitorFrame fallback;
  if(!frame){fallback.session=currentFile.toStdString()+"/retained";for(const auto &r:model.records)fallback.consume(r);}
  for(int c=0;c<2;++c)if(auto tree=rtViews[c]) {
    if(auto panel=tree->parentWidget())panel->setVisible(rtGrouped);if(paneViews[c])paneViews[c]->setVisible(!rtGrouped);
    if(!rtGrouped||rtDisplayPaused)continue;
    const auto &filter=paneModels[c];rtModels[c].update(frame?*frame:fallback,c,filter.busFilter);
    if(!tree->property("widthsSet").toBool()){const int widths[]={110,135,135,85,125,90};for(int i=0;i<6;++i)tree->setColumnWidth(i,widths[i]);tree->setProperty("widthsSet",true);}
    tree->setColumnHidden(1,filter.busFilter=="B");tree->setColumnHidden(2,filter.busFilter=="A");
    for(int row=0;row<rtModels[c].rowCount();++row){const auto parent=rtModels[c].index(row,0);const auto rt=parent.data(Qt::UserRole+1).toInt();bool matching=false;
      for(int child=0;child<rtModels[c].rowCount(parent);++child){const auto i=rtModels[c].index(child,0,parent);const auto r=rtModels[c].recordFor(i);const bool visible=r&&(filter.busFilter.isEmpty()||filter.busFilter==(r->bus?"B":"A"))&&(filter.search.isEmpty()||words(*r).contains(filter.search,Qt::CaseInsensitive));tree->setRowHidden(child,parent,!visible);matching|=visible;}
      tree->setRowHidden(row,{},(filter.rtFilter>=0&&rt!=filter.rtFilter)||!matching);
    }
    if(!tree->currentIndex().isValid())for(int row=0;row<rtModels[c].rowCount();++row)if(!tree->isRowHidden(row,{})){tree->setCurrentIndex(rtModels[c].index(row,0));break;}
  }
}
