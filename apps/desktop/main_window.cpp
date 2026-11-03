#include "main_window.hpp"
#include "services/key_gate.hpp"
#include "services/runtime_paths.hpp"
#include "services/ui_observer.hpp"
#include "ui_main_window.h"
#include "ui_page_header.h"
#include "ui_workspace.h"
#include "ui_page_footer.h"
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#include "services/workspace_style.hpp"

namespace desktop {
namespace {std::array<aero::TimingHistogram,8> updateStages;}
void resetGuiUpdateStages(){updateStages={};}
std::array<aero::TimingHistogram,8> guiUpdateStages(){return updateStages;}
}

Window::Window()
      : settings(desktop::preferencesPath(),
                 QSettings::IniFormat){
    setWindowTitle("AeroTrace 1553 "+QCoreApplication::applicationVersion()+" — Bus Analysis Studio");
    setMinimumSize(900, 540);
    const auto available =
        QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1500, available.width() - 32),
           qMin(940, available.height() - 48));
    if(settings.value("sessionStorageVersion",0).toInt()<2) {
      auto old=settings.value("recordFolder",desktop::dataDirectory()+"/Sessions").toString();
      auto roots=settings.value("legacySessionRoots").toStringList();if(!roots.contains(old))roots<<old;
      settings.setValue("legacySessionRoots",roots);settings.setValue("recordFolder",desktop::defaultSessionDirectory());settings.setValue("sessionStorageVersion",2);
    }
    sessionDir=settings.value("recordFolder",desktop::defaultSessionDirectory()).toString();
    QDir().mkpath(sessionDir);
    backend.captureConfig.hardware=settings.value("captureSource","DDC").toString()=="DDC";
    backend.captureConfig.logicalDevices={settings.value("logicalChannel1",0).toInt(),settings.value("logicalChannel2",1).toInt()};
    backend.captureConfig.simulatorRate=settings.value("simulatorRate",12).toUInt();
    backend.captureConfig.runtime=std::filesystem::path(settings.value("ddcRuntime").toString().toStdWString());
    QFile savedProfile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/active-profile.json");
    if(savedProfile.open(QIODevice::ReadOnly)) {
      std::vector<aero::Signal> mappings;
      if(desktop::profileFromJson(QJsonDocument::fromJson(savedProfile.readAll()).object(),mappings).isEmpty()) backend.applyProfile(std::move(mappings));
    }
    model.refresh();
    Ui::MainWindow form;
    form.setupUi(this);
    setWindowTitle("AeroTrace 1553 "+QCoreApplication::applicationVersion()+" — Bus Analysis Studio");
    resize(qMin(1500, available.width() - 32),
           qMin(940, available.height() - 48));
    root = form.centralWidget;
    headerTitle = form.headerTitle;
    footerBadge = form.footerBadge;
    licenseBadge = form.licenseBadge;
    stateBadge = form.stateBadge;
    scroll = form.workspaceScroll;
    auto styleBadge = [](QLabel *label, const QString &fg, const QString &bg) {
      label->setFixedHeight(28);
      label->setAlignment(Qt::AlignCenter);
      label->setStyleSheet(QString("color:%1;background:%2;border-radius:14px;padding:6px 10px;font-size:10px;").arg(fg,bg));
    };
    styleBadge(form.simulationBadge, "#526057", "#f0f2ee");
    form.simulationBadge->setText(backend.captureConfig.hardware ? "DDC · RECEIVE ONLY" : "SIMULATOR");
    styleBadge(licenseBadge, "#cf405b", "#fff2f5");
    styleBadge(stateBadge, "#626e60", "#ffffff");
    connect(form.preferencesButton, &QPushButton::clicked, this, [this] { navigate(16); });
    for (int i = 0; i < 18; ++i) {
      auto b = form.navigation->findChild<QPushButton *>(QString("nav%1").arg(i));
      Q_ASSERT(b);
      connect(b, &QPushButton::clicked, this, [this, i] {
        if (i != 0 && !unlocked) {
          pendingPage = i;
          navigate(0);
          toast("Open the local workspace to continue.");
          return;
        }
        navigate(i);
      });
      nav << b;
    }
    connect(&tick, &QTimer::timeout, this, [this] { updateLive(); });
    tick.start(100);
    captureClock.start();
    setupOperatorShell();
    observeUiActions(this);
    applicationEvent("workspace_paths","",{{"preferences",settings.fileName()},{"recordings",sessionDir},{"data",desktop::dataDirectory()}});
    setupKeyGate();
    if(QStandardPaths::isTestModeEnabled() || desktop::keyRequired()) navigate(0);
    else {
      unlocked=true; navigate(4);
      QTimer::singleShot(350,this,[this]{automaticCapture();});
    }
    shortcut(QKeySequence("Ctrl+O"), [this] {
      if (unlocked)
        openSession();
    });
    shortcut(QKeySequence("Ctrl+R"), [this] {
      if (unlocked)
        toggleCapture();
    });
    shortcut(QKeySequence("F1"), [this] {
      if (unlocked)
        navigate(17);
    });
    shortcut(QKeySequence(Qt::Key_Space), [this] {
      if (page == 11) {
        backend.replayPlaying = !backend.replayPlaying;
        replayClock.restart();
      }
    });
  }

Window::~Window(){
  tick.stop();configurationRetry.stop();++*replayGeneration;
  // Tear down document callbacks while Window state and form widgets still live.
  if(auto dialog=documentDialog.data()){dialog->close();delete dialog;}
  for(auto w:findChildren<QFutureWatcherBase *>())w->waitForFinished();
  backend.acquisition.stop();
  for(auto view:findChildren<QAbstractItemView *>()) {
    if(view->selectionModel())QObject::disconnect(view->selectionModel(),nullptr,nullptr,nullptr);
  }
  // Views and their selection callbacks must die before the value-owned models/backend.
  delete takeCentralWidget();
}

void Window::shortcut(QKeySequence k, std::function<void()> f){
    auto s = new QShortcut(k, this);
    connect(s, &QShortcut::activated, this, f);
    connect(s,&QShortcut::activated,this,[k]{applicationEvent("shortcut",k.toString());});
  }





void Window::toast(QString text){ applicationEvent("operator_notice",text);statusBar()->showMessage(text, 8000); }





void Window::navigate(int n){
    applicationEvent("navigate","",{{"from",page},{"to",n},{"unlocked",unlocked}});
    if(n<0 || n>=int(desktop::pages.size())) return;
    if (n != 0 && !unlocked) {
      for (int i = 0; i < nav.size(); ++i)
        nav[i]->setChecked(i == page);
      toast("Open the local workspace first.");
      return;
    }
    persistMonitorWorkspace();
    page = n;
    plots.clear();
    paneViews={};paneCounts={};monitorSplitter=nullptr;monitorWorkspace=nullptr;channelWindows={};tabMonitor=nullptr;rtLights={};rtDetail=nullptr;rtLegend=nullptr;runNameEdit=nullptr;csvFolderEdit=nullptr;outputPreview=nullptr;
    monitor = nullptr;
    scrub = nullptr;
    replayTime = nullptr;
    totalMetric = nullptr;
    engineeringLabels.clear();
    qualityTable = nullptr;
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
    headerTitle->setText(names[n]);
    for (int i = 0; i < nav.size(); ++i)
      {nav[i]->setChecked(i == n);
       const auto original=nav[i]->property("navigationIcon").value<QIcon>();if(!original.isNull()){auto pixmap=original.pixmap(22,22);if(i==n){QPainter painter(&pixmap);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),Qt::white);}nav[i]->setIcon(QIcon(pixmap));}}

    auto old = scroll->takeWidget();
    if (old)
      old->deleteLater();
    content = new QWidget;
    Ui::Workspace workspaceForm;
    workspaceForm.setupUi(content);
    pageLayout = workspaceForm.pageLayout;
    if (n == 0) {
      licensePage();
      finishPage();
      return;
    }
    auto heading = new QWidget;
    Ui::PageHeader headingForm;
    headingForm.setupUi(heading);
    headingForm.pageTitle->setText(n == 1 ? "Welcome back" : n == 8 ? "Bus & RT Topology" : names[n]);
    headingForm.pageSubtitle->setText(subtitle(n));
    headingForm.eyebrow->hide();headingForm.pageSubtitle->hide();headingForm.pageTitle->hide();
    if(n==4) heading->hide();
    titleActions = headingForm.pageActionsLayout;
    pageLayout->addWidget(heading);
    switch (n) {
    case 1:
      welcome();
      break;
    case 2:
      device();
      break;
    case 3:
      dashboard();
      break;
    case 4:
      liveMonitor();
      break;
    case 5:
      inspector();
      break;
    case 6:
      engineering();
      break;
    case 7:
      plotPage();
      break;
    case 8:
      topology();
      break;
    case 9:
      events();
      break;
    case 10:
      sessions();
      break;
    case 11:
      replay();
      break;
    case 12:
      icdLibrary();
      break;
    case 13:
      editor();
      break;
    case 14:
      exports();
      break;
    case 15:
      security();
      break;
    case 16:
      preferences();
      break;
    case 17:
      diagnostics();
      break;
    }
    auto footer = new QLabel;
    Ui::PageFooter footerForm;
    footerForm.setupUi(footer);
    footer->hide();
    if(n!=4) pageLayout->addStretch();
    refreshDataViews();
    refreshOperatorState();
    finishPage();
  }

void Window::finishPage(){
    if(page>=1&&page<=3)desktop::polishWorkspace(content);
    observeUiActions(content);
    pageLayout->activate();
    scroll->setWidget(content);
    content->show();
    scroll->verticalScrollBar()->setValue(0);
  }

QString Window::subtitle(int n){
    return QStringList{
        "",
        "Connect to your DDC interface or continue a local recording.",
        "Discover the DDC USB interface and configure read-only monitor "
        "channels.",
        "Live overview of observed bus activity, signal health and recording "
        "integrity.",
        "Filter and inspect captured raw MIL-STD-1553 traffic in real time.",
        "Word-by-word inspection with bit fields, source metadata and matching "
        "ICD signals.",
        "Live parameter values derived from a versioned ICD profile.",
        "Synchronized engineering traces with windowing, cursors and "
        "display-only pause.",
        "Passive visualization of observed controllers and remote-terminal "
        "addresses.",
        "Review protocol errors, integrity notices and derived engineering "
        "anomalies.",
        "Search, inspect and recover locally recorded capture sessions.",
        "Reconstruct a recorded session using original timestamps and "
        "versioned ICD mappings.",
        "Versioned mappings convert hexadecimal payload words to engineering "
        "parameters.",
        "Create or edit one engineering signal and preview conversion against "
        "raw data.",
        "Create timestamp-bound datasets and integrity-aware engineering "
        "reports.",
        "Inspect the local physical dongle and permitted offline product "
        "features.",
        "Customize display and recording behavior while preserving acquisition "
        "integrity.",
        "Verify build compatibility, acquisition health and offline "
        "installation requirements."}
        .value(n);
  }



void Window::connectDevices(){
  if(!unlocked)return;
  if(connecting||savingSession||closingCapture||backend.acquisition.active())return;
  if(discoveryRunning){toast("Wait for the DDC scan to finish");return;}
  if(!settings.contains("logicalChannel1")&&!settings.contains("logicalChannel2")) {
    const auto assignments=hardwareDiscovery.value("suggested_logical_devices").toArray();
    if(assignments.size()==2){for(int i=0;i<2;++i){backend.captureConfig.logicalDevices[i]=assignments[i].toInt();settings.setValue(QString("logicalChannel%1").arg(i+1),backend.captureConfig.logicalDevices[i]);}}
    else {toast("Choose and verify the DDC logical channels in Device Setup");navigate(2);return;}
  }
  for(int ch=1;ch<=2;++ch)for(int b=0;b<2;++b)backend.captureConfig.enabled[(ch-1)*2+b]=settings.value(QString("channel%1Bus %2").arg(ch).arg(b?"B":"A"),true).toBool();
  automaticHeld=false;connectionIsRecording=false;connecting=true;
  applicationEvent("devices_connect_requested");
  backend.captureConfig.diagnostic=[](const std::string &operation,int result,const std::string &detail){applicationEvent("ddc_connection_api",QString::fromStdString(operation),{{"result",result},{"detail",QString::fromStdString(detail)}},result?QtWarningMsg:QtInfoMsg);};
  connectionFuture=backend.acquisition.beginConnection(backend.captureConfig);
  refreshOperatorState();
}
void Window::disconnectDevices(){
  if(connecting||savingSession)return;
  if(backend.acquisition.active()){disconnectAfterSave=true;stopAndSave();return;}
  backend.acquisition.stop();applicationEvent("devices_disconnected");refreshOperatorState();
}

void Window::startCapture(){
    if(backend.captureConfig.hardware&&!backend.acquisition.connected()){toast("Connect devices before starting a recording");return;}
    if(backend.captureConfig.hardware&&discoveryRunning){toast("Wait for the DDC scan to finish before connecting");return;}
    applicationEvent("capture_request","",{{"hardware",backend.captureConfig.hardware},{"recordings",sessionDir},{"profile_count",int(backend.mappings().size())},{"run_id",applicationRunId()}});
    if(connecting || savingSession || closingCapture) return;
    automaticHeld=false;
    if (!unlocked)
      return;
    if (backend.acquisition.active()) {
      navigate(3);
      return;
    }
    sessionSaveState.clear();
    QStorageInfo storage(sessionDir);
    if (storage.bytesAvailable() < 100 * 1024 * 1024) {
      toast("Insufficient free space: at least 100 MB is required.");
      return;
    }
    currentFile = QDir(sessionDir)
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
    if(!backend.captureConfig.simulatorProfile.empty())backend.captureConfig.simulatorProfile=backend.mappings();
    if(!backend.captureConfig.hardware)if(auto error=aero::validateSimulation(backend.captureConfig);!error.empty()){toast(QString::fromStdString(error));return;}
    if(backend.captureConfig.hardware) {
      backend.captureConfig.enabled=enabled;
      if(const auto error=backend.acquisition.setProfile(backend.mappings());!error.empty()) {toast(QString::fromStdString(error));return;}
      connectionIsRecording=true;connecting=true;
      connectionFuture=backend.acquisition.beginConfigured(std::filesystem::path(currentFile.toStdWString()),backend.captureConfig);
      refreshOperatorState();return;
    }
    ++*replayGeneration;replaySeekPending=false;replayActivity={};
    if (!backend.startRecording(std::filesystem::path(currentFile.toStdWString()), enabled)) {
      toast(QString::fromStdString(backend.acquisition.metrics(false).error));
      return;
    }
    if(auto error=desktop::describeSession(currentFile,backend.captureConfig);!error.isEmpty())toast(error);
    model.records.clear();
    model.refresh();
    captureClock.restart();
    stateBadge->setText("● RECORDING");
    eventText = backend.captureConfig.hardware ? "DDC receive-only capture started" : "Simulator capture started";
    navigate(QStandardPaths::isTestModeEnabled() ? 3 : 4);
  }

void Window::toggleCapture(){
    if(connecting) {automaticHeld=true;backend.acquisition.requestStop();return;}
    if (backend.acquisition.active()) {
      stopAndSave();
    } else
      startCapture();
  }

void Window::updateLive(){
    QElapsedTimer updateElapsed;updateElapsed.start();
    const unsigned viewStage=page==4?(rtGrouped?0:1):page==7?2:3;
    struct ObserveUpdate {aero::TimingHistogram &h;QElapsedTimer &t;unsigned mode;~ObserveUpdate(){const auto ns=t.nsecsElapsed();h.observe(ns);desktop::updateStages[mode].observe(ns);}} observeUpdate{guiUpdateTiming,updateElapsed,viewStage};
    static QElapsedTimer heartbeat;static qint64 previousHeartbeat=0;
    if(!heartbeat.isValid())heartbeat.start();const auto nowHeartbeat=heartbeat.elapsed();
    if(previousHeartbeat && nowHeartbeat-previousHeartbeat>qMax(500,tick.interval()*4))applicationEvent("ui_heartbeat_delay","",{{"gap_ms",nowHeartbeat-previousHeartbeat},{"page",page},{"recording",currentFile}});previousHeartbeat=nowHeartbeat;
    if(!applicationLogError().isEmpty())statusBar()->showMessage(applicationLogError());
    finishConnection();
    auto incoming = backend.acquisition.drain();
    if (!incoming.empty()) {
      if(!backend.selected.id) backend.selected=incoming.front();
      const auto cacheBegan=updateElapsed.nsecsElapsed();
      if (backend.accept(incoming)) {
        eventText = "In-memory analysis window trimmed; raw recording retained";
      }
      desktop::updateStages[4].observe(updateElapsed.nsecsElapsed()-cacheBegan);
      if (!displayPaused) {
        const auto modelsBegan=updateElapsed.nsecsElapsed();
        model.append(incoming);
        if(monitor && (!monitorWorkspace || !monitorWorkspace->isVisible()) && selectedMonitorId) for(int row=0;row<model.rowCount();++row)
          if(model.at(row)->id==selectedMonitorId) {monitor->selectionModel()->setCurrentIndex(model.index(row,selectedMonitorColumn),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);break;}
        refreshMonitorPanes(true);
        if (monitor && (!monitorWorkspace || !monitorWorkspace->isVisible()) && settings.value("autoscroll", true).toBool())
          monitor->scrollToBottom();
        desktop::updateStages[5].observe(updateElapsed.nsecsElapsed()-modelsBegan);
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
    if (!incoming.empty()) {
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
    if(updateElapsed.elapsed()>100)applicationEvent("ui_update_slow","",{{"duration_ms",updateElapsed.elapsed()},{"page",page},{"incoming",qint64(incoming.size())},{"recording",currentFile}});
  }

void Window::closeEvent(QCloseEvent *e){
    applicationEvent("workspace_closing","",{{"capture",currentFile},{"active",backend.acquisition.active()}});
    persistMonitorWorkspace();
    automaticHeld=true;
    if(savingSession){toast("Wait for Stop & save to finish before closing.");e->ignore();return;}
    if(backend.acquisition.active()||connecting){
      e->ignore();if(closingCapture)return;closingCapture=true;statusBar()->showMessage("Closing: stopping capture and preserving the complete recording…");backend.acquisition.requestStop();
      auto watcher=new QFutureWatcher<void>(this);connect(watcher,&QFutureWatcher<void>::finished,this,[this,watcher]{if(connectionFuture.valid())connectionFuture.get();connecting=false;closingCapture=false;watcher->deleteLater();QTimer::singleShot(0,this,&QWidget::close);});watcher->setFuture(QtConcurrent::run([this]{backend.acquisition.stop();}));return;
    }
    if(closingCapture){e->ignore();return;}
    if(documentDialog)documentDialog->close();if(taskDialog)taskDialog->close();
    applicationEvent("workspace_closed","",{{"capture",currentFile},{"recorded",double(backend.acquisition.metrics(false).recorded)},{"capture_error",QString::fromStdString(backend.acquisition.metrics(false).error)}});
    settings.sync();
    e->accept();
  }
