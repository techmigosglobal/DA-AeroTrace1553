#include <QTabBar>
#include "main_window.hpp"
#include "services/runtime_paths.hpp"
#include "services/ui_observer.hpp"
#include "ui_main_window.h"
#include "ui_page_header.h"
#include "ui_workspace.h"
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#include "services/workspace_style.hpp"
#include "services/ddc_discovery.hpp"

namespace desktop {
namespace {std::array<aero::TimingHistogram,8> updateStages;}
void resetGuiUpdateStages(){updateStages={};}
std::array<aero::TimingHistogram,8> guiUpdateStages(){return updateStages;}
}

Window::Window()
      : settings(desktop::preferencesPath(),
                 QSettings::IniFormat){
    setMinimumSize(900, 540);
    const auto available =
        QGuiApplication::primaryScreen()->availableGeometry();
    sessionDir=settings.value("exportFolder",desktop::defaultExportDirectory()).toString();
    if(QFileInfo::exists(desktop::dataDirectory()+"/pending-icd-deletion.json")) {
      setProperty("icdDeletePending",true);
      QTimer::singleShot(0,this,&Window::resumeIcdDeletion);
    }
    restoreCaptureSource();
    backend.captureConfig.logicalDevices={settings.value("logicalChannel1",0).toInt(),settings.value("logicalChannel2",1).toInt()};
    backend.captureConfig.simulatorRate=settings.value("simulatorRate",12).toUInt();
    backend.captureConfig.runtime=std::filesystem::path(settings.value("ddcRuntime").toString().toStdWString());
    backend.captureConfig.backend=settings.value("ddcBackend","native").toString()=="helper"?aero::DdcBackend::HelperX86:aero::DdcBackend::NativeX64;
    backend.captureConfig.receiveMode=static_cast<aero::DdcReceiveMode>(qBound(0,settings.value("ddcReceiveMode",0).toInt(),2));
    backend.captureConfig.connectionPolicy=settings.value("ddcCompatibility",false).toBool()?aero::DdcConnectionPolicy::Compatibility:aero::DdcConnectionPolicy::QualifiedAutomatic;
    backend.captureConfig.helperPack=std::filesystem::path(settings.value("ddcHelperPack").toString().toStdWString());
    QFile previousConnections(desktop::dataDirectory()+"/connection-latest.json");if(previousConnections.open(QIODevice::ReadOnly)){
      const auto previous=QJsonDocument::fromJson(previousConnections.readAll()).object();connectionAttempts=previous.value("attempts").toArray();connectionAttempt=previous.value("latest_attempt").toObject();
    }
    QFile savedProfile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/active-profile.json");
    if(savedProfile.open(QIODevice::ReadOnly)) {
      std::vector<aero::Signal> mappings;
      const auto profile=QJsonDocument::fromJson(savedProfile.readAll()).object();
      manualRegistry=profile["manual_library"].toObject();
      if(auto error=desktop::profileFromJson(profile,mappings);error.isEmpty()) backend.applyProfile(std::move(mappings));
      else qWarning().noquote()<<"Active ICD profile rejected:"<<savedProfile.fileName()<<error;
    }
    model.refresh();
    Ui::MainWindow form;
    form.setupUi(this);
    setWindowTitle("AeroTrace 1553 "+QCoreApplication::applicationVersion()+" — Capture & ICD Workspace");
    resize(qMin(1500, available.width() - 32),
           qMin(940, available.height() - 48));
    root = form.centralWidget;
    headerTitle = form.headerTitle;
    stateBadge = form.stateBadge;
    scroll = form.workspaceScroll;
    workspaceStack = form.workspaceStack;
    auto styleBadge = [](QLabel *label, const QString &fg, const QString &bg) {
      label->setFixedHeight(28);
      label->setAlignment(Qt::AlignCenter);
      label->setStyleSheet(QString("color:%1;background:%2;border-radius:14px;padding:6px 10px;font-size:10px;").arg(fg,bg));
    };
    styleBadge(form.simulationBadge, "#145331", "#e2f2e8");
    form.simulationBadge->setText(backend.captureConfig.hardware ? "DDC · RECEIVE ONLY" : "SIMULATOR");
    styleBadge(stateBadge, "#626e60", "#ffffff");
    for (int i = 0; i < 18; ++i) {
      auto b = form.navigation->findChild<QPushButton *>(QString("nav%1").arg(i));
      if (b) {
        connect(b, &QPushButton::clicked, this, [this, i] { navigate(i); });
      }
      nav << b;
    }
    connect(&tick, &QTimer::timeout, this, [this] { updateLive(); });
    tick.start(100);
    captureClock.start();
    setupOperatorShell();
    observeUiActions(this);
    applicationEvent("workspace_paths","",{{"preferences",settings.fileName()},{"recordings",sessionDir},{"data",desktop::dataDirectory()}});
    unlocked = true;
    navigate(2);
    if (!QStandardPaths::isTestModeEnabled()) {
      QTimer::singleShot(350, this, [this] { automaticCapture(); });
    }
  }

Window::~Window(){
  tick.stop();configurationRetry.stop();++*replayGeneration;
  for(auto w:findChildren<QFutureWatcherBase *>())w->waitForFinished();
  backend.acquisition.stop();
  for(auto view:findChildren<QAbstractItemView *>()) {
    if(view->selectionModel())QObject::disconnect(view->selectionModel(),nullptr,nullptr,nullptr);
  }
  // Views and their selection callbacks must die before the value-owned models/backend.
  delete takeCentralWidget();
}

void Window::toast(QString text){ applicationEvent("operator_notice",text);statusBar()->showMessage(text, 8000); }

void Window::navigate(int n){
    QElapsedTimer navigationTime;
    navigationTime.start();
    applicationEvent("navigate","",{{"from",page},{"to",n}});
    if(n<0 || n>=int(desktop::pages.size())) return;
    if(n==0 || n==1 || n==15) n=2;
    if(n==5 || n==8 || n==9) n=4;
    if(n==13) n=12;
    if(scroll)settings.setValue(QString("pageScroll/%1").arg(page),scroll->verticalScrollBar()->value());
    persistMonitorWorkspace();
    const auto workspacePage=[](int number){return number==6 || number==7 || number==12;};
    page = n;
    if(n==6 || n==7 || n==12){
      const auto scope=backend.replaySource?"replay/"+currentFile:QString("live");
      if(scope!=workspaceScopeKey){
        workspaceScopeKey=scope;workspaceSubsystem=settings.value("workspace/"+scope+"/subsystem").toString();
        const auto mappings=backend.mappings();const auto selected=std::find_if(mappings.begin(),mappings.end(),[this](const auto &signal){return inWorkspace(signal);});
        if(selected!=mappings.end()){preferredSignalIndex=unsigned(selected-mappings.begin());if(selected->channel)analysisChannel=selected->channel;}
      }
    }
    plots.clear();
    paneViews={};paneCounts={};monitorSplitter=nullptr;monitorWorkspace=nullptr;channelWindows={};tabMonitor=nullptr;rtLights={};rtDetail=nullptr;rtLegend=nullptr;runNameEdit=nullptr;csvFolderEdit=nullptr;outputPreview=nullptr;
    monitor = nullptr;
    scrub = nullptr;
    replayTime = nullptr;
    totalMetric = nullptr;
    engineeringLabels.clear();
    qualityTable = nullptr;
    engineeringDetails=nullptr;
    replayTable = nullptr;
    busAMetric = nullptr;
    busBMetric = nullptr;
    diskMetric = nullptr;
    cursorALabel = nullptr;
    cursorBLabel = nullptr;
    cursorDeltaLabel = nullptr;
    cursorAValue.reset();
    monitorStatus = nullptr;
    deviceTabs=nullptr;deviceTitles={};deviceStatuses={};selectedSummary=nullptr;monitorCsv=nullptr;
    sessionTable = nullptr;
    const auto route=(n==7)?6:(n==11)?10:n;
    const QString routeTitle=route==4?"Monitor & Record":route==6?"Engineering & Plots":route==10?"Sessions & Replay":route==12?"Signal Mappings":names[route];
    updateShellHeading(route,routeTitle,route==16?QString():subtitle(n));

    if (auto old = scroll->widget()) {
      old->hide();
      scroll->takeWidget()->deleteLater();
    } else if(content) {
      content->deleteLater();
    }
    const bool stagedAnalysis=n==7 && backend.acquisition.active();
    content = new QWidget(stagedAnalysis?this:nullptr);
    Ui::Workspace workspaceForm;
    workspaceForm.setupUi(content);
    pageLayout = workspaceForm.pageLayout;
    auto heading = new QWidget;
    Ui::PageHeader headingForm;
    headingForm.setupUi(heading);
    headingForm.pageTitle->setText(names[n]);
    headingForm.pageSubtitle->setText(subtitle(n));
    headingForm.eyebrow->hide();headingForm.pageSubtitle->hide();headingForm.pageTitle->hide();
    if(n==4 || n==12) heading->hide();
    titleActions = headingForm.pageActionsLayout;
    pageLayout->addWidget(heading);
    if(n==6 || n==7) {
      auto tabs=new QTabBar(content);
      tabs->setObjectName("subWorkspaceTabs");
      tabs->setAccessibleName("Engineering and Plots");
      tabs->addTab("Engineering Table");
      tabs->addTab("Analysis Plots");
      tabs->setCurrentIndex(n==6?0:1);
      pageLayout->addWidget(tabs);
      connect(tabs,&QTabBar::currentChanged,this,[this,activeTabs=QPointer<QTabBar>(tabs)](int index){
        if(!activeTabs || !content->isAncestorOf(activeTabs) || index<0 || index>1) return;
        const auto focused=activeTabs->hasFocus();
        navigate(index==0?6:7);
        if(focused) if(auto next=content->findChild<QTabBar *>("subWorkspaceTabs")) next->setFocus(Qt::TabFocusReason);
      },Qt::QueuedConnection);
    } else if(n==10 || n==11) {
      auto tabs=new QTabBar(content);
      tabs->setObjectName("subWorkspaceTabs");
      tabs->setAccessibleName("Sessions and Replay");
      tabs->addTab("Saved Sessions");
      tabs->addTab("Replay Timeline");
      tabs->setCurrentIndex(n==10?0:1);
      pageLayout->addWidget(tabs);
      connect(tabs,&QTabBar::currentChanged,this,[this,activeTabs=QPointer<QTabBar>(tabs)](int index){
        if(!activeTabs || !content->isAncestorOf(activeTabs) || index<0 || index>1) return;
        const auto focused=activeTabs->hasFocus();
        navigate(index==0?10:11);
        if(focused) if(auto next=content->findChild<QTabBar *>("subWorkspaceTabs")) next->setFocus(Qt::TabFocusReason);
      },Qt::QueuedConnection);
    }

    const auto setupMs=navigationTime.elapsed();
    switch (n) {
    case 2:
      device();
      break;
    case 3:
      dashboard();
      break;
    case 4:
      liveMonitor();
      break;
    case 6:
      engineering();
      break;
    case 7:
      plotPage();
      break;
    case 10:
      sessions();
      break;
    case 11:
      replay();
      break;
    case 12:
      signalMappingsPage();
      break;
    case 14:
      exports();
      break;
    case 16:
      preferences();
      break;
    case 17:
      diagnostics();
      break;
    }
    const auto formMs=navigationTime.elapsed();
    if(n!=4 && n!=6 && n!=7 && n!=16) pageLayout->addStretch();
    refreshDataViews();
    refreshOperatorState();
    const auto refreshMs=navigationTime.elapsed();
    if(stagedAnalysis) {
      // Let input and the recording indicator update before laying out the
      // chart. A newer navigation request owns the scroll area from then on.
      QTimer::singleShot(std::chrono::milliseconds(16),this,
        [this,target=QPointer<QWidget>(content),navigationTime,setupMs,formMs,refreshMs]{
          if(!target || target!=content)return;
          QElapsedTimer layoutTime;
          layoutTime.start();
          finishPage();
          if(navigationTime.elapsed()>100)
            applicationEvent("slow_navigation","",{{"page",page},{"shell_ms",setupMs},{"form_ms",formMs-setupMs},
              {"refresh_ms",refreshMs-formMs},{"layout_ms",layoutTime.elapsed()},{"total_ms",navigationTime.elapsed()}});
        });
      return;
    }
    finishPage();
    const auto totalMs=navigationTime.elapsed();
    if(totalMs>100 && backend.acquisition.active())
      applicationEvent("slow_navigation","",{{"page",n},{"shell_ms",setupMs},{"form_ms",formMs-setupMs},
        {"refresh_ms",refreshMs-formMs},{"layout_ms",totalMs-refreshMs},{"total_ms",totalMs}});
  }

void Window::finishPage(){
    if(page>=1&&page<=3)desktop::polishWorkspace(content);
    observeUiActions(content);
    QList<Plot *> charts;
    if(page==7 && !recordedCsv && backend.acquisition.active()) {
      for(auto plot:plots)if(!plot->isHidden()) {
        auto sizing=plot->sizePolicy();
        sizing.setRetainSizeWhenHidden(true);
        plot->setSizePolicy(sizing);
        plot->hide();
        charts.append(plot);
      }
    }
    scroll->setWidget(content);
    content->show();
    updateEngineeringDetails();
    scroll->verticalScrollBar()->setValue(settings.value(QString("pageScroll/%1").arg(page),0).toInt());
    if(!charts.isEmpty())
      QTimer::singleShot(std::chrono::milliseconds(16),content,[charts]{
        for(auto plot:charts)plot->show();
      });
  }

bool Window::inWorkspace(const aero::Signal &signal) const {
  return workspaceSubsystem.isEmpty() || QString::fromStdString(signal.subsystem)==workspaceSubsystem;
}

void Window::openSignalMappingForRoute(const PreFillMappingRoute &route) {
  pendingPreFillRoute=route;
  navigate(12);
}

QString Window::subtitle(int n){
    return QStringList{
        "",
        "Connect to your DDC interface or continue a local recording.",
        "Step 1 · Select DDC hardware or simulation mode, channels, and output folder.",
        "Live RT 00–31 bus activity grid.",
        "Step 2 · Capture MIL-STD-1553 bus traffic, view RT 00–31 matrix, inspect details, or export CSV.",
        "",
        "Step 4 · Inspect converted engineering values and preview raw-to-engineering formulas.",
        "Step 4 · Synchronized engineering traces with Cursor, Pan, Zoom, Statistics, and Limits.",
        "",
        "",
        "Step 5 · Browse, replay, and inspect saved local .atd recordings.",
        "Step 5 · Reconstruct a recorded session along its timeline.",
        "Step 3 · Define RT/SA/Tx commands and 0-based Word 0–31 signal fields manually or from JSON/CSV.",
        "",
        "Step 5 · Export time-bounded CSV, JSON, or Engineering datasets with SHA-256 checksums.",
        "Inspect local security and product entitlements.",
        "Customize display behavior, radix formatting, and application defaults.",
        "Step-by-step guided workflow help, system health checks, and support bundles."}
        .value(n);
  }



void Window::connectDevices(){
  if(!unlocked)return;
  if(connecting||savingSession||closingCapture||backend.acquisition.active())return;
  if(discoveryRunning){toast("Wait for the DDC scan to finish");return;}
  if(backend.acquisition.metrics(false).cleanupBlocked){toast("Previous SDK cleanup failed. Review connection details before reconnecting.");return;}
  for(int ch=1;ch<=2;++ch)for(int b=0;b<2;++b)backend.captureConfig.enabled[(ch-1)*2+b]=settings.value(QString("channel%1Bus %2").arg(ch).arg(b?"B":"A"),true).toBool();
  if(!connectionAssignmentsFresh) {
    if(!connectionAttempt.isEmpty()){connectionAttempts.append(connectionAttempt);while(connectionAttempts.size()>32)connectionAttempts.removeAt(0);}
    QJsonArray requested;for(bool on:backend.captureConfig.enabled)requested.append(on);
    connectionAttempt={{"attempt_id",QUuid::createUuid().toString(QUuid::Id128)},{"started_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {"state","checking_assignments"},{"requested_streams",requested},{"backend",backend.captureConfig.backend==aero::DdcBackend::NativeX64?"native_x64":"helper_x86"},
      {"receive_mode",int(backend.captureConfig.receiveMode)},{"connection_policy",backend.captureConfig.connectionPolicy==aero::DdcConnectionPolicy::Compatibility?"compatibility":"qualified_automatic"},
      {"helper_manifest",QString::fromStdWString(backend.captureConfig.helperPack.wstring())},{"api_events",QJsonArray{}}};
    connectAfterDiscovery=true;saveConnectionEvidence();rescanDdc();return;
  }
  connectionAssignmentsFresh=false;
  const auto resolved=desktop::resolveDdcAssignments(hardwareDiscovery,backend.captureConfig.enabled,backend.captureConfig.logicalDevices,
    {settings.contains("logicalChannel1"),settings.contains("logicalChannel2")},settings.value("ddcAssignmentMode","card_manager").toString()=="manual");
  connectionAttempt["assignment_resolution"]=resolved;
  if(!resolved.value("valid").toBool()){finishConnectionEvidence(false,resolved.value("error").toString());navigate(2);return;}
  const auto ids=resolved.value("logical_devices").toArray();
  for(int i=0;i<2;++i)if(backend.captureConfig.enabled[i*2]||backend.captureConfig.enabled[i*2+1]){backend.captureConfig.logicalDevices[i]=ids[i].toInt();settings.setValue(QString("logicalChannel%1").arg(i+1),ids[i].toInt());}
  connectionAttempt["runtime_path"]=hardwareDiscovery.value("runtime_path");connectionAttempt["runtime_sha256"]=hardwareDiscovery.value("runtime_sha256");
  connectionAttempt["provider_id"]=hardwareDiscovery.value("provider_id");connectionAttempt["provider_path"]=hardwareDiscovery.value("provider_path");
  connectionAttempt["helper_sha256"]=hardwareDiscovery.value("helper_sha256");connectionAttempt["header_sha256"]=hardwareDiscovery.value("header_sha256");
  connectionAttempt["qualification"]=hardwareDiscovery.value("qualification");connectionAttempt["state"]="connecting";saveConnectionEvidence();
  automaticHeld=false;connectionIsRecording=false;connecting=true;
  applicationEvent("devices_connect_requested");
  const auto attemptId=connectionAttempt.value("attempt_id").toString();QPointer<Window> receiver(this);
  backend.captureConfig.diagnostic=[receiver,attemptId](const std::string &operation,int result,const std::string &detail){
    const auto op=QString::fromStdString(operation),text=QString::fromStdString(detail);applicationEvent("ddc_connection_api",op,{{"result",result},{"detail",text},{"attempt_id",attemptId}},result?QtWarningMsg:QtInfoMsg);
    if(receiver)QMetaObject::invokeMethod(receiver,[receiver,attemptId,op,result,text]{if(!receiver||receiver->connectionAttempt.value("attempt_id")!=attemptId)return;
      auto events=receiver->connectionAttempt.value("api_events").toArray();events.append(QJsonObject{{"operation",op},{"result",result},{"detail",text},{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});while(events.size()>256)events.removeAt(0);receiver->connectionAttempt["api_events"]=events;
      if(op=="runtime_loaded")receiver->connectionAttempt["loaded_runtime_detail"]=text;
    },Qt::QueuedConnection);
  };
  connectionFuture=backend.acquisition.beginConnection(backend.captureConfig);
  refreshOperatorState();
}
void Window::disconnectDevices(){
  if(connecting||savingSession)return;
  if(backend.acquisition.active()){disconnectAfterSave=true;stopAndSave();return;}
  backend.acquisition.stop();applicationEvent("devices_disconnected");refreshOperatorState();
}

void Window::startCapture(){
    if(property("icdDeletePending").toBool()){toast("Finish pending profile deletion before recording.");return;}
    if(backend.captureConfig.hardware&&!backend.acquisition.connected()){toast("Connect devices before starting a recording");return;}
    if(backend.captureConfig.hardware&&discoveryRunning){toast("Wait for the DDC scan to finish before connecting");return;}
    applicationEvent("capture_request","",{{"hardware",backend.captureConfig.hardware},{"recordings",sessionDir},{"profile_count",int(backend.mappings().size())},{"run_id",applicationRunId()}});
    if(connecting || savingSession || closingCapture) return;
    automaticHeld=false;
    if (!unlocked)
      return;
    if (backend.acquisition.active()) {
      navigate(4);
      return;
    }
    sessionSaveState.clear();
    const auto selectedFolder=settings.value("exportFolder").toString().trimmed();
    const auto output=QDir(selectedFolder.isEmpty()?desktop::defaultExportDirectory():selectedFolder).absolutePath();
    if(!QDir().mkpath(output)){toast("Cannot create the selected recording folder.");return;}
    QStorageInfo storage(output);
    if (storage.bytesAvailable() < 100 * 1024 * 1024) {
      toast("Insufficient free space: at least 100 MB is required.");
      return;
    }
    auto runName=settings.value("nextRunName").toString().trimmed();
    if(runName.isEmpty())runName=desktop::suggestedRunName(output);
    if(const auto error=desktop::validateRunName(runName);!error.isEmpty()){toast(error);return;}
    for(const auto &suffix:{"_EXT.csv","_INT.csv"})
      if(QFileInfo::exists(QDir(output).filePath(runName+suffix))){toast("The output folder already contains this run name. Choose a new name.");return;}
    settings.setValue("exportFolder",output);settings.setValue("nextRunName",runName);
    currentFile = QDir(output)
                      .filePath("Capture_" +
                                QDateTime::currentDateTime().toString(
                                    "yyyyMMdd_HHmmss_zzz") +
                                ".atd");
    std::array<bool, 4> enabled{};
    for (int ch = 1; ch <= 2; ++ch)
      for (int b = 0; b < 2; ++b)
        enabled[(ch - 1) * 2 + b] =
            settings
                .value(QString("channel%1Bus %2").arg(ch).arg(b ? "B" : "A"),
                       true)
                .toBool();
    if (std::none_of(enabled.begin(), enabled.end(),
                     [](bool v) { return v; })) {
      toast("Enable at least one channel/bus in Device Setup.");
      return;
    }
    backend.captureConfig.enabled=enabled;
    backend.captureConfig.runId=applicationRunId().toStdString();
    applicationEvent("capture_file","",{{"path",currentFile},{"hardware",backend.captureConfig.hardware},{"device_1",backend.captureConfig.logicalDevices[0]},{"device_2",backend.captureConfig.logicalDevices[1]}});
    if(!backend.captureConfig.hardware)if(auto error=aero::validateSimulation(backend.captureConfig);!error.empty()){toast(QString::fromStdString(error));return;}
      viewingReplayStatistics=false;rtDisplayPaused=false;
    if(backend.captureConfig.hardware) {
      backend.captureConfig.enabled=enabled;
      if(const auto error=backend.acquisition.setProfile(backend.captureMappings());!error.empty()) {toast(QString::fromStdString(error));return;}
      connectionIsRecording=true;connecting=true;
      connectionFuture=backend.acquisition.beginConfigured(std::filesystem::path(currentFile.toStdWString()),backend.captureConfig);
      refreshOperatorState();return;
    }
    ++*replayGeneration;replaySeekPending=false;replayActivity={};
    if (!backend.startRecording(std::filesystem::path(currentFile.toStdWString()), enabled)) {
      toast(QString::fromStdString(backend.acquisition.metrics(false).error));
      return;
    }
    if(auto error=desktop::describeSession(currentFile,backend.captureConfig,runName,desktop::csvOptions(settings));!error.isEmpty())toast(error);
    preserveSessionConnection(currentFile);
    model.records.clear();
    model.refresh();
    captureClock.restart();
    stateBadge->setText("● RECORDING");
    eventText = backend.captureConfig.hardware ? "DDC receive-only capture started" : "Simulator capture started";
    navigate(4);
  }

void Window::toggleCapture(){
    if(connecting) {automaticHeld=true;backend.acquisition.requestStop();return;}
    if (backend.acquisition.active()) {
      stopAndSave();
    } else
      startCapture();
  }

void Window::updateLive(){
    if(backend.acquisition.active())inference.pauseForCapture();
    QElapsedTimer updateElapsed;updateElapsed.start();
    const unsigned viewStage=page==4?(rtGrouped?0:1):page==7?2:3;
    struct ObserveUpdate {aero::TimingHistogram &h;QElapsedTimer &t;unsigned mode;~ObserveUpdate(){const auto ns=t.nsecsElapsed();h.observe(ns);desktop::updateStages[mode].observe(ns);}} observeUpdate{guiUpdateTiming,updateElapsed,viewStage};
    static QElapsedTimer heartbeat;static qint64 previousHeartbeat=0;
    if(!heartbeat.isValid())heartbeat.start();const auto nowHeartbeat=heartbeat.elapsed();
    if(previousHeartbeat && nowHeartbeat-previousHeartbeat>qMax(500,tick.interval()*4))applicationEvent("ui_heartbeat_delay","",{{"gap_ms",nowHeartbeat-previousHeartbeat},{"page",page},{"recording",currentFile}});previousHeartbeat=nowHeartbeat;
    if(!applicationLogError().isEmpty())statusBar()->showMessage(applicationLogError());
    finishConnection();
    // Keep the GUI batch cost bounded even when recording is producing faster
    // than the table can paint. The durable writer consumes its own queue.
    auto incoming = backend.acquisition.drain(2048);
    const auto incomingCount=incoming.size();
    if (incomingCount) {
      if(!backend.selected.id) backend.selected=incoming.front();
      const auto modelsBegan=updateElapsed.nsecsElapsed();
      if(!displayPaused)model.append(incoming);
      const auto modelAppendTime=updateElapsed.nsecsElapsed()-modelsBegan;
      const auto cacheBegan=updateElapsed.nsecsElapsed();
      if (backend.accept(std::move(incoming))) {
        eventText = "In-memory analysis window trimmed; raw recording retained";
      }
      desktop::updateStages[4].observe(updateElapsed.nsecsElapsed()-cacheBegan);
      if (!displayPaused) {
        const auto panesBegan=updateElapsed.nsecsElapsed();
        if(monitor && (!monitorWorkspace || !monitorWorkspace->isVisible()) && selectedMonitorId) for(int row=0;row<model.rowCount();++row)
          if(model.at(row)->id==selectedMonitorId) {monitor->selectionModel()->setCurrentIndex(model.index(row,selectedMonitorColumn),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);break;}
        refreshMonitorPanes(true);
        if (monitor && (!monitorWorkspace || !monitorWorkspace->isVisible()) && settings.value("autoscroll", true).toBool())
          monitor->scrollToBottom();
        desktop::updateStages[5].observe(modelAppendTime+updateElapsed.nsecsElapsed()-panesBegan);
      }
    }
    const auto chromeBegan=updateElapsed.nsecsElapsed();
    if (backend.acquisition.error()) {
      stateBadge->setText("● CAPTURE ERROR");
      const auto message=QString::fromStdString(backend.acquisition.metrics(false).error);
      if(eventText!=message) {eventText=message;toast(eventText);}
    }
    if (totalMetric)
      totalMetric->setText(QString::number(backend.acquisition.metrics(false).recorded));
    if (busAMetric)
      busAMetric->setText(QString::number(countBus(0)));
    if (busBMetric)
      busBMetric->setText(QString::number(countBus(1)));
    if (diskMetric)
      diskMetric->setText(fileSize(currentFile));
    const auto health=backend.acquisition.metrics(false);
    if (monitorStatus)
      monitorStatus->setText(
          QString("Recorded %1   ·   Displayed %2   ·   Display queue %3 / 8192   ·   Display skips %4   ·   Hardware lost %5 / overflows %6%7")
              .arg(health.recorded).arg(model.rowCount()).arg(health.queueDepth)
              .arg(health.displayDrops).arg(health.hardwareLost).arg(health.hardwareOverflows).arg(health.failed?"   ·   INCOMPLETE — inspect diagnostics":""));
    refreshOperatorState();
    refreshRtDashboard();
    desktop::updateStages[6].observe(updateElapsed.nsecsElapsed()-chromeBegan);
    if (incomingCount) {
      const auto dataBegan=updateElapsed.nsecsElapsed();
      refreshDataViews();
      desktop::updateStages[7].observe(updateElapsed.nsecsElapsed()-dataBegan);
    }
    if (backend.replayPlaying && backend.replayCount() && !replaySeekPending) {
      const auto delta=replayClock.restart();const auto target=backend.replayPositionNs+delta*1000000.*backend.replaySpeed;auto lo=backend.replayIndex,hi=backend.replayCount();
      while(lo<hi){const auto mid=lo+(hi-lo)/2;if((backend.recordingIndex?backend.recordingIndex->playbackTimestamp(mid):backend.replayTimestamp(mid))<=target)lo=mid+1;else hi=mid;}
      const auto next=lo?lo-1:0;
      if(next!=backend.replayIndex)seekReplay(next);if(!replaySeekPending)backend.replayPositionNs=target;
      if(next+1>=backend.replayCount())backend.replayPlaying=false;
    }
    if(updateElapsed.elapsed()>100)applicationEvent("ui_update_slow","",{{"duration_ms",updateElapsed.elapsed()},{"page",page},{"incoming",qint64(incomingCount)},{"recording",currentFile}});
  }

void Window::closeEvent(QCloseEvent *e){
    applicationEvent("workspace_closing","",{{"capture",currentFile},{"active",backend.acquisition.active()}});
    if(scroll)settings.setValue(QString("pageScroll/%1").arg(page),scroll->verticalScrollBar()->value());
    persistMonitorWorkspace();
    automaticHeld=true;
    if(savingSession){toast("Wait for Stop & save to finish before closing.");e->ignore();return;}
    if(backend.acquisition.active()||connecting){
      e->ignore();if(closingCapture)return;closingCapture=true;statusBar()->showMessage("Closing: stopping capture and preserving the complete recording…");backend.acquisition.requestStop();
      auto watcher=new QFutureWatcher<void>(this);connect(watcher,&QFutureWatcher<void>::finished,this,[this,watcher]{if(connectionFuture.valid())connectionFuture.get();connecting=false;closingCapture=false;watcher->deleteLater();QTimer::singleShot(0,this,&QWidget::close);});watcher->setFuture(QtConcurrent::run([this]{backend.acquisition.stop();}));return;
    }
    if(closingCapture){e->ignore();return;}
    applicationEvent("workspace_closed","",{{"capture",currentFile},{"recorded",double(backend.acquisition.metrics(false).recorded)},{"capture_error",QString::fromStdString(backend.acquisition.metrics(false).error)}});
    settings.sync();
    e->accept();
  }
