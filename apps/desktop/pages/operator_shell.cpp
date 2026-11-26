#include "main_window.hpp"
#include "services/csv_conversion.hpp"
#include "services/profile_io.hpp"
#include "services/session_store.hpp"
#include "services/runtime_paths.hpp"
#include "services/ddc_discovery.hpp"
#include "services/operator_style.hpp"
#include "widgets/startup_splash.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#endif

void Window::setupOperatorShell() {
  auto footerIcon=findChild<QLabel *>("footerIcon");
  footerIcon->setFixedSize(38,38);
  desktop::labelIcon(footerIcon,"aircraft",34);
  if(auto footerCaption=findChild<QLabel *>("sidebarBrandCaption"))
    footerCaption->setText(QString("v%1\nReceive-only").arg(QCoreApplication::applicationVersion()));
  findChild<QHBoxLayout *>("sidebarFooterLayout")->setStretch(1,1);
  applicationEvent("workspace_started",backend.captureConfig.hardware ? "DDC receive-only source selected" : "Explicit simulator source selected");
  setAcceptDrops(true);configurationRetry.setSingleShot(true);configurationRetry.setInterval(800);
  connect(&configurationRetry,&QTimer::timeout,this,[this]{automaticCapture();});
  sidebar=findChild<QFrame *>("sidebar");
  sidebarToggle=findChild<QPushButton *>("sidebarToggle");
  captureButton=findChild<QPushButton *>("globalCaptureButton");
  captureSummary=new QLabel(this);
  captureSummary->setAccessibleName("Recording integrity and output");
  statusBar()->addPermanentWidget(captureSummary);
  captureSummary->hide();
  const QStringList icons={"shield","home","device","dashboard","activity","search","sliders","chart","grid","bell","folder","play","file","edit","download","shield","settings","help"};
  for(int i=0;i<nav.size();++i) {
    if(!nav[i]) continue;
    nav[i]->setProperty("fullText",nav[i]->text());
    const QIcon icon(":/icons/"+icons[i]+".svg");
    QIcon navigationIcon;
    navigationIcon.addPixmap(icon.pixmap(22,22),QIcon::Normal,QIcon::Off);
    auto selected=icon.pixmap(22,22);
    {
      QPainter painter(&selected);
      painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
      painter.fillRect(selected.rect(),Qt::white);
    }
    navigationIcon.addPixmap(selected,QIcon::Normal,QIcon::On);
    nav[i]->setIcon(navigationIcon);
    nav[i]->setIconSize(QSize(21,21));
    nav[i]->setAccessibleName(names[i]);nav[i]->setToolTip(names[i]);
  }
  desktop::actionIcon(captureButton,"link",true);
  sidebarToggle->setAccessibleName("Collapse navigation");sidebarToggle->setToolTip("Collapse navigation");
  connect(sidebarToggle,&QPushButton::clicked,this,[this]{setSidebarCollapsed(!sidebarCollapsed);});
  auto navigation=findChild<QVBoxLayout *>("navigationLayout");
  csvNavigation=new QPushButton("CSV Viewer");
  csvNavigation->setObjectName("csvNavigation");
  recoveryNavigation=new QPushButton("Recover Recording");
  recoveryNavigation->setObjectName("recoveryNavigation");
  for(auto button:{csvNavigation,recoveryNavigation}) {
    button->setProperty("navigation",true);
    button->setProperty("fullText",button->text());
    button->setAccessibleName(button->text());
    button->setToolTip(button->text());
  }
  desktop::actionIcon(csvNavigation,"file");
  desktop::actionIcon(recoveryNavigation,"folder");
  connect(csvNavigation,&QPushButton::clicked,this,[this]{
    if(auto old=scroll->widget()){old->hide();scroll->takeWidget()->deleteLater();}
    else if(content)content->deleteLater();
    page=18;
    updateShellHeading(18,"CSV Viewer","Preview raw data, convert between formats, plot, or map to Signal Mappings.");
    content=new QWidget;
    auto lay=new QVBoxLayout(content);
    lay->setContentsMargins(16,12,16,14);
    lay->setSpacing(0);
    lay->addWidget(buildCsvViewerWidget(content),1);
    scroll->setWidget(content);
    refreshOperatorState();
  });
  connect(recoveryNavigation,&QPushButton::clicked,this,[this]{findUnsavedRecordings();});
  for(const auto &entry:std::array<std::pair<int,QString>,3>{
      {{2,"Device Setup"},{14,"Exports & Reports"},{17,"Help & Diagnostics"}}}) {
    auto button=nav[entry.first];
    if(!button) continue;
    auto caption=entry.second;
    caption.replace("&","&&");
    button->setText(caption);
    button->setProperty("fullText",caption);
    button->setAccessibleName(entry.second);
    button->setToolTip(entry.second);
  }
  const std::array destinations{nav[2],nav[4],nav[12],nav[6],
    nav[10],csvNavigation,nav[14],nav[16],nav[17],recoveryNavigation};
  auto divider=new QFrame;
  divider->setObjectName("sidebarRule");
  divider->setFixedHeight(1);
  for(int i=0;i<int(destinations.size());++i) {
    if(!destinations[i]) continue;
    navigation->removeWidget(destinations[i]);
    navigation->insertWidget(i+(i>=7),destinations[i]);
    destinations[i]->show();
    if(i==6)navigation->insertWidget(7,divider);
  }
  QWidget *previous=sidebarToggle;
  for(auto button:destinations){if(button){QWidget::setTabOrder(previous,button);previous=button;}}
  QWidget::setTabOrder(previous,captureButton);

  if(auto rightLayout=findChild<QVBoxLayout *>("rightLayout")) {
    workflowStepper=new QFrame(findChild<QWidget *>("rightPanel"));
    workflowStepper->setObjectName("workflowStepper");
    auto stepLayout=new QHBoxLayout(workflowStepper);
    stepLayout->setContentsMargins(10,6,10,6);
    stepLayout->setSpacing(6);
    const std::array<std::pair<QString,int>,5> stepDefs{{
      {"1. Setup Device",2},
      {"2. Monitor && Record",4},
      {"3. Signal Mappings",12},
      {"4. Engineering && Plots",6},
      {"5. Sessions && Exports",10}
    }};
    for(int s=0;s<5;++s) {
      auto btn=new QPushButton(stepDefs[s].first,workflowStepper);
      btn->setProperty("stepButton",true);
      btn->setProperty("stepState","pending");
      btn->setCursor(Qt::PointingHandCursor);
      const int targetPage=stepDefs[s].second;
      connect(btn,&QPushButton::clicked,this,[this,targetPage]{navigate(targetPage);});
      workflowStepButtons[s]=btn;
      stepLayout->addWidget(btn,1);
      if(s<4) {
        auto arrow=new QLabel("›",workflowStepper);
        arrow->setProperty("tone","muted");
        arrow->setAlignment(Qt::AlignCenter);
        stepLayout->addWidget(arrow);
      }
    }
    signalPillButton=new QPushButton("No mappings · Map Word 0–31 →",workflowStepper);
    signalPillButton->setObjectName("signalPillButton");
    signalPillButton->setCursor(Qt::PointingHandCursor);
    signalPillButton->setToolTip("Open Signal Mappings");
    connect(signalPillButton,&QPushButton::clicked,this,[this]{navigate(12);});
    stepLayout->addSpacing(6);
    stepLayout->addWidget(signalPillButton,0);
    rightLayout->insertWidget(1,workflowStepper);
    workflowStepper->hide();
    rightLayout->setStretch(0,0);
    rightLayout->setStretch(1,0);
    rightLayout->setStretch(2,1);
  }

  connect(captureButton,&QPushButton::clicked,this,[this] {
    if(connecting) {automaticHeld=true;backend.acquisition.requestStop();}
    else if(backend.acquisition.connected() || backend.acquisition.active()) disconnectDevices();
    else if(backend.captureConfig.hardware) connectDevices();
    else startCapture();
  });
  setSidebarCollapsed(settings.value("sidebarCollapsed",false).toBool());
  auto setNavText=[this](QPushButton *button,const QString &label){
    if(!button)return;
    button->setText(sidebarCollapsed?QString():label);
    button->setProperty("fullText",label);
    auto accessible=label;accessible.replace("&&","&");
    button->setAccessibleName(accessible);
    button->setToolTip(accessible);
  };
  setNavText(nav[2],"Device Setup");
  setNavText(nav[4],"Live Monitor");
  setNavText(nav[12],"Signal Mappings");
  setNavText(nav[6],"Engineering");
  setNavText(nav[10],"Sessions");
  if(nav[6])nav[6]->setToolTip("Engineering & Plots · Table, formulas, and synchronized Qwt plots");
  if(nav[10])nav[10]->setToolTip("Sessions & Replay · Open saved .atd recordings and replay timeline");
  if(nav[12])nav[12]->setToolTip("Signal Mappings · Configure manual RT/SA/Word 0–31 fields or import JSON/CSV");
  if(!QStandardPaths::isTestModeEnabled() && settings.value("operatorWorkflowVersion",0).toInt()<2) {
    for(int ch=1;ch<=2;++ch) for(const auto &bus:{"A","B"}) settings.setValue(QString("channel%1Bus %2").arg(ch).arg(bus),true);
    settings.setValue("operatorWorkflowVersion",2);
  }
}

void Window::setSidebarCollapsed(bool collapsed) {
  sidebarCollapsed=collapsed;
  sidebar->setFixedWidth(collapsed?66:width()>=1550?240:210);
  const bool wide=width()>=1550;
  findChild<QLabel *>("brandText")->setText("AeroTrace\n1553");
  findChild<QLabel *>("brandText")->setStyleSheet(wide?"font-size:17px;font-weight:700;color:#ffffff;":"font-size:15px;font-weight:700;color:#ffffff;");
  findChild<QVBoxLayout *>("sidebarLayout")->setContentsMargins(collapsed?7:10,collapsed?10:12,collapsed?7:10,14);
  findChild<QHBoxLayout *>("brandLayout")->setDirection(collapsed?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight);
  findChild<QHBoxLayout *>("brandLayout")->setContentsMargins(collapsed?4:12,10,collapsed?4:8,10);
  const auto brand=findChild<QLabel *>("brandIcon");
  const int edge=collapsed?28:34;
  brand->setFixedSize(edge,edge);
  brand->setPixmap(QIcon(":/icons/aerotrace.svg").pixmap(QSize(edge,edge),devicePixelRatioF()));
  findChild<QLabel *>("brandText")->setVisible(!collapsed);
  findChild<QLabel *>("brandSubtitle")->setVisible(false);
  findChild<QWidget *>("sidebarFooter")->setVisible(!collapsed);
  auto wave=findChild<QLabel *>("sidebarWave");
  wave->setFixedHeight(0);
  wave->setVisible(false);
  findChild<QFrame *>("sidebarRule")->setVisible(!collapsed);
  sidebarToggle->setIcon(QIcon(collapsed?":/icons/expand.svg":":/icons/collapse.svg"));
  sidebarToggle->setAccessibleName(collapsed?"Expand navigation":"Collapse navigation");
  sidebarToggle->setToolTip(collapsed?"Expand navigation":"Collapse navigation");
  auto buttons=nav;
  buttons<<csvNavigation<<recoveryNavigation;
  for(auto button:buttons) {
    if(!button) continue;
    button->setText(collapsed?"":button->property("fullText").toString());
    button->setProperty("collapsed",collapsed);
    button->style()->unpolish(button);
    button->style()->polish(button);
  }
  settings.setValue("sidebarCollapsed",collapsed);
  sidebar->layout()->activate();
  root->layout()->activate();
}

void Window::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  if(!sidebar)return;
  const bool wide=event->size().width()>=1550;
  auto brandText=findChild<QLabel *>("brandText");
  const QString brandSize=wide?"font-size:17px;font-weight:700;color:#ffffff;":"font-size:15px;font-weight:700;color:#ffffff;";
  if(brandText->styleSheet()!=brandSize)brandText->setStyleSheet(brandSize);
  auto headerLayout=findChild<QHBoxLayout *>("headerLayout");
  headerLayout->setContentsMargins(18,wide?18:14,18,wide?18:14);
  auto sidebarLayout=findChild<QVBoxLayout *>("sidebarLayout");
  sidebarLayout->setContentsMargins(sidebarCollapsed?7:10,sidebarCollapsed?10:12,sidebarCollapsed?7:10,14);
  if(!sidebarCollapsed) {
    findChild<QLabel *>("sidebarWave")->setVisible(false);
    sidebar->setFixedWidth(wide?240:210);
    const auto brand=findChild<QLabel *>("brandIcon");
    const QSize edge=QSize(34,34);
    if(brand->size()!=edge) {
      brand->setFixedSize(edge);
      brand->setPixmap(QIcon(":/icons/aerotrace.svg").pixmap(edge,devicePixelRatioF()));
    }
  }
  findChild<QLabel *>("simulationBadge")->setVisible(page!=4 && width()>=1060);
  auto subtitleLabel=findChild<QLabel *>("headerSubtitle");
  subtitleLabel->setVisible(width()>=1150&&!subtitleLabel->text().isEmpty());
}

void Window::updateShellHeading(int route,const QString &title,const QString &description) {
  headerTitle->setText(title);
  auto subtitleLabel=findChild<QLabel *>("headerSubtitle");
  subtitleLabel->setText(description);
  subtitleLabel->setVisible(width()>=1150&&!description.isEmpty());
  static const QHash<int,QString> icons{{-1,"sparkle"},{2,"device"},{4,"activity"},
    {6,"chart"},{10,"folder"},{12,"file"},{14,"download"},{15,"shield"},{16,"settings"},{17,"help"},{18,"file"}};
  desktop::labelIcon(findChild<QLabel *>("headerIcon"),icons.value(route,"dashboard"),28);
  for(int i=0;i<nav.size();++i) if(nav[i]) nav[i]->setChecked(i==route);
  if(csvNavigation) csvNavigation->setChecked(route==18);
  refreshWorkflowStepper();
}

void Window::refreshWorkflowStepper() {
  if(workflowStepper) workflowStepper->setVisible(false);
  const auto activeCount=backend.mappings().size();
  if(signalPillButton) {
    signalPillButton->setVisible(true);
    signalPillButton->setText(activeCount
      ? QString("Signals: %1 active →").arg(activeCount)
      : QString("No mappings · Map Word 0–31 →"));
  }
  const int currentStep=(page==2)?0:(page==3||page==4)?1:(page==12||page==13)?2:(page==6||page==7)?3:(page==10||page==11||page==14||findChild<QDialog *>("savedCsvTools"))?4:-1;
  const bool step1Done=backend.acquisition.connected() || backend.acquisition.active() || !backend.archive.empty() || !currentFile.isEmpty();
  const bool step2Done=step1Done && (!backend.archive.empty() || !currentFile.isEmpty() || backend.replaySource);
  const bool step3Done=step2Done && activeCount>0;
  const bool step4Done=step3Done && (!backend.archive.empty() || backend.replaySource);
  const bool step5Done=step4Done && sessionSaveState=="Complete";
  const std::array<bool,5> done{step1Done,step2Done,step3Done,step4Done,step5Done};
  for(int s=0;s<5;++s) {
    auto btn=workflowStepButtons[s];
    if(!btn) continue;
    const QString nextState=(s==currentStep)?"current":done[s]?"done":"pending";
    if(btn->property("stepState").toString()!=nextState) {
      btn->setProperty("stepState",nextState);
      btn->style()->unpolish(btn);
      btn->style()->polish(btn);
    }
  }
}

void Window::automaticCapture() { /* Recording always requires an explicit action. */ }
QString Window::recordingDisabledReason() const {
  if(connectAfterDiscovery)return "Checking current Card Manager assignments…";
  if(connecting)return "Connection in progress — wait or cancel the connection.";
  if(savingSession)return "Saving recording — wait for finalization.";
  if(closingCapture)return "Application cleanup in progress.";
  const auto m=backend.acquisition.metrics(false);
  if(m.cleanupBlocked)return m.error.empty()?QString("SDK ownership release is unconfirmed. Collect diagnostics."):QString("Connection blocked · %1").arg(QString::fromStdString(m.error));
  if(backend.captureConfig.hardware&&!backend.acquisition.connected()) {
    const auto cause=connectionAttempt.value("error").toString();
    return cause.isEmpty()?QString("Connect devices before starting a recording."):"Connect failed: "+cause;
  }
  return backend.acquisition.active()?QString("Receiving and saving the requested channels."):QString("Connected — Start recording begins receiving and saving together.");
}
void Window::saveConnectionEvidence() {
  const auto path=desktop::dataDirectory()+"/connection-latest.json";QDir().mkpath(QFileInfo(path).absolutePath());
  const QJsonObject report{{"schema","aerotrace.connection-evidence.v1"},{"application_version",QCoreApplication::applicationVersion()},
    {"application_run_id",applicationRunId()},{"latest_attempt",connectionAttempt},{"attempts",connectionAttempts},{"discovery",hardwareDiscovery}};
  QSaveFile output(path);const auto bytes=QJsonDocument(report).toJson();if(output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size())output.commit();
}
void Window::preserveSessionConnection(const QString &recording) {
  if(recording.isEmpty())return;
  const auto connection=backend.captureConfig.hardware?connectionAttempt:QJsonObject{{"source","explicit_simulation"},{"hardware_connected",false}};
  QSaveFile sidecar(recording+".connection.json");const auto evidence=QJsonDocument(connection).toJson();if(sidecar.open(QIODevice::WriteOnly)&&sidecar.write(evidence)==evidence.size())sidecar.commit();
  QFile input(recording+".session.json");if(!input.open(QIODevice::ReadOnly))return;auto descriptor=QJsonDocument::fromJson(input.readAll()).object();input.close();
  descriptor["connection"]=connection;QSaveFile output(recording+".session.json");const auto bytes=QJsonDocument(descriptor).toJson();if(output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size())output.commit();
}
void Window::finishConnectionEvidence(bool ok,const QString &error) {
  const auto previousState=connectionAttempt.value("state").toString();const auto metrics=backend.acquisition.metrics(false);
  connectionAttempt["state"]=ok?"connected":"failed";connectionAttempt["finished_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  connectionAttempt["error"]=ok?QString():error;
  if(previousState=="connecting") {
    const auto &f=metrics.failure;QJsonArray recovery;for(const auto &action:f.recovery)recovery.append(QString::fromStdString(action));
    connectionAttempt["failure"]=QJsonObject{{"category",int(f.category)},{"stage",QString::fromStdString(f.stage)},{"operation",QString::fromStdString(f.operation)},
      {"native_code",f.code},{"channel",f.channel},{"logical_device",f.logicalDevice},{"detail",QString::fromStdString(f.detail)},
      {"windows_code",int(f.windowsCode)},{"windows_context_supplementary",f.supplementaryWindows},{"recovery",recovery}};
    connectionAttempt["cleanup_blocked"]=metrics.cleanupBlocked;
    const auto &identity=metrics.identity;
    connectionAttempt["resolved_identity"]=QJsonObject{{"backend",QString::fromStdString(identity.backend)},{"receive_mode",QString::fromStdString(identity.receiveMode)},
      {"runtime_path",QString::fromStdWString(identity.runtime.wstring())},{"runtime_sha256",QString::fromStdString(identity.runtimeSha256)},
      {"provider_id",QString::fromStdString(identity.providerId)},{"provider_path",QString::fromStdWString(identity.provider.wstring())},
      {"helper_path",QString::fromStdWString(identity.helper.wstring())},{"helper_manifest",QString::fromStdWString(identity.manifest.wstring())},{"helper_sha256",QString::fromStdString(identity.helperSha256)},
      {"sdk_identity",QString::fromStdString(identity.sdkIdentity)},{"header_sha256",QString::fromStdString(identity.headerSha256)},{"qualification",QString::fromStdString(identity.qualification)}};
    if(!identity.runtime.empty())connectionAttempt["runtime_path"]=QString::fromStdWString(identity.runtime.wstring());
    if(!identity.runtimeSha256.empty())connectionAttempt["runtime_sha256"]=QString::fromStdString(identity.runtimeSha256);
    if(!identity.providerId.empty())connectionAttempt["provider_id"]=QString::fromStdString(identity.providerId);
    if(!identity.qualification.empty())connectionAttempt["qualification"]=QString::fromStdString(identity.qualification);
  }
  QJsonArray channelResults;for(int c=0;c<2;++c)channelResults.append(QJsonObject{{"channel",c},{"phase",int(metrics.devices[c].phase)},{"message",QString::fromStdString(metrics.devices[c].message)}});
  connectionAttempt["channel_results"]=channelResults;saveConnectionEvidence();
  applicationEvent("connection_result",error,connectionAttempt,ok?QtInfoMsg:QtWarningMsg);
  if(!ok&&!error.isEmpty())toast(error);
}
void Window::connectWorkingChannel(unsigned channel) {
  if(channel>1||connecting||connectAfterDiscovery||savingSession||backend.acquisition.active()||backend.acquisition.connected())return;
  for(unsigned c=0;c<2;++c)for(unsigned b=0;b<2;++b)settings.setValue(QString("channel%1Bus %2").arg(c+1).arg(b?"B":"A"),c==channel?backend.captureConfig.enabled[c*2+b]:false);
  connectionAttempt["operator_selected_partial_channel"]=int(channel);saveConnectionEvidence();connectDevices();
}
void Window::connectCompatibilityPack() {
  if(connecting||connectAfterDiscovery||savingSession||backend.acquisition.connected()||backend.acquisition.active())return;
  if(discoveryRunning){toast("Wait for the SDK scan to finish before selecting a compatibility pack.");return;}
  if(backend.captureConfig.backend==aero::DdcBackend::HelperX86) {
    if(backend.captureConfig.helperPack.empty()){toast("Select a verified legacy helper pack in Device Setup.");navigate(2);return;}
  }else {
    QStringList labels,paths;
    for(const auto &value:hardwareDiscovery.value("runtime_candidates").toArray()) {const auto c=value.toObject();
      if(!c.value("usable").toBool()||!c.value("provider_available").toBool()||!c.value("error").toString().isEmpty())continue;
      paths<<c.value("path").toString();labels<<QString("%1 · %2 · %3").arg(c.value("provider_id").toString(),c.value("file_version").toString(),c.value("path").toString());
    }
    if(labels.isEmpty()){toast("No exact software-verified runtime/provider pair is available. Rescan or install a verified provider pack.");navigate(2);return;}
    bool accepted=false;const auto selected=QInputDialog::getItem(this,"Compatibility connection — physical qualification NOT RUN","Choose the exact software-verified runtime/provider pair",labels,0,false,&accepted);
    if(!accepted)return;const auto path=paths.value(labels.indexOf(selected));if(path.isEmpty())return;
    backend.captureConfig.runtime=std::filesystem::path(path.toStdWString());settings.setValue("ddcRuntime",path);
  }
  backend.captureConfig.connectionPolicy=aero::DdcConnectionPolicy::Compatibility;settings.setValue("ddcCompatibility",true);configurationChanged();connectDevices();
}
QWidget *Window::connectionResultPanel(bool recordingSetup) {
  auto panel=new QFrame;panel->setObjectName("connectionResultPanel");panel->setProperty("card",!recordingSetup);auto layout=new QVBoxLayout(panel);layout->setContentsMargins(8,4,8,4);layout->setSpacing(2);
  auto actions=new QHBoxLayout;
  layout->addLayout(actions);
  if(recordingSetup) {
    auto icon=new QLabel;
    desktop::labelIcon(icon,"database",23);
    actions->addWidget(icon);
    auto title=new QLabel("Recording Setup");
    title->setProperty("sectionTitle",true);
    actions->addWidget(title);
    actions->addStretch();
  }
  auto detailsButton=new QPushButton("Connection details");
  detailsButton->setObjectName("openConnectionDetails");
  desktop::actionIcon(detailsButton,"info");
  actions->addWidget(detailsButton);
  connect(detailsButton,&QPushButton::clicked,this,[this,detailsButton]{
    auto dialog=new QDialog(this);
    dialog->setObjectName("connectionDetailsDialog");
    dialog->setWindowTitle("Connection details");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(qMin(660,width()-80),340);
    auto body=new QVBoxLayout(dialog);
    body->addWidget(desktop::sectionHeading("Connection details","info"));
    auto detail=new QPlainTextEdit(dialog);
    detail->setObjectName("connectionAttemptDetails");
    detail->setReadOnly(true);
    detail->setAccessibleName("Connection evidence");
    detail->setPlainText(connectionAttempt.isEmpty()?"No connection attempt recorded.":
                         QString::fromUtf8(QJsonDocument(connectionAttempt).toJson()));
    body->addWidget(detail,1);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);
    body->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(dialog,&QDialog::finished,detailsButton,[detailsButton]{detailsButton->setFocus();});
    if(desktop::startupMotionEnabled())dialog->setWindowOpacity(0);
    dialog->open();
    if(dialog->windowOpacity()<1) {
      auto fade=new QPropertyAnimation(dialog,"windowOpacity",dialog);
      fade->setDuration(desktop::operatorTransitionMs);
      fade->setEndValue(1.0);
      fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
  });
  auto copy=new QPushButton("Copy details");
  copy->setObjectName("copyConnectionDetails");
  desktop::actionIcon(copy,"copy");
  actions->addWidget(copy);
  connect(copy,&QPushButton::clicked,panel,[this]{QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(connectionAttempt).toJson()));});
  auto chooseMethod=new QPushButton("Choose connection method");
  chooseMethod->setObjectName("chooseConnectionMethod");
  desktop::actionIcon(chooseMethod,"link");
  actions->addWidget(chooseMethod);
  connect(chooseMethod,&QPushButton::clicked,this,[this]{navigate(2);});
  auto collect=new QPushButton("Collect diagnostics");
  desktop::actionIcon(collect,"activity");
  actions->addWidget(collect);
  connect(collect,&QPushButton::clicked,this,&Window::collectDiagnostics);
  auto manager=new QPushButton("Open Card Manager");
  desktop::actionIcon(manager,"device");
  actions->addWidget(manager);
  connect(manager,&QPushButton::clicked,this,&Window::openCardManager);
  if(!recordingSetup)actions->addStretch();
  // On the monitor, keep capture feedback with the recording setup row.
  auto summary=new QLabel;
  summary->setObjectName("connectionResultSummary");
  summary->setWordWrap(true);
  summary->setProperty("tone","warning");
  summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(summary);
  auto reason=new QLabel;
  reason->setObjectName("recordingDisabledReason");
  reason->setWordWrap(true);
  reason->setProperty("tone","muted");
  reason->setAccessibleName("Recording readiness and disabled reason");
  layout->addWidget(reason);
  const bool animate=desktop::startupMotionEnabled();
  auto busy=new QProgressBar;
  busy->setProperty("operatorBusy",true);
  busy->setTextVisible(false);
  busy->setRange(0,0);
  layout->addWidget(busy);
  auto recovery=new QHBoxLayout;layout->addLayout(recovery);auto compatibility=new QPushButton("Connect using selected pack…");compatibility->setObjectName("connectCompatibilityPack");recovery->addWidget(compatibility);connect(compatibility,&QPushButton::clicked,this,&Window::connectCompatibilityPack);
  std::array<QPushButton *,2> partial{};for(unsigned c=0;c<2;++c){partial[c]=new QPushButton(QString("Connect CH%1 only").arg(c+1));partial[c]->setObjectName(QString("connectChannel%1Only").arg(c+1));recovery->addWidget(partial[c]);connect(partial[c],&QPushButton::clicked,panel,[this,c]{connectWorkingChannel(c);});}recovery->addStretch();
  const auto refresh=[this,panel,summary,reason,recordingSetup,compatibility,partial,manager,detailsButton,copy,chooseMethod,collect,recovery,busy,animate]{const auto m=backend.acquisition.metrics(false);const bool idle=!connecting&&!connectAfterDiscovery&&!savingSession&&!backend.acquisition.connected()&&!backend.acquisition.active()&&!closingCapture&&!m.cleanupBlocked;
    const auto state=connectionAttempt.value("state").toString();const auto error=connectionAttempt.value("error").toString();
    const bool showFailure=connecting||connectAfterDiscovery||m.failed||m.cleanupBlocked||(state=="failed"&&!error.isEmpty());
    panel->setVisible(recordingSetup||showFailure);
    QString text=connectionAttempt.isEmpty()?QString("Connect configures the selected channels. Start recording begins bus reception."):QString("Last attempt %1 · %2").arg(connectionAttempt.value("attempt_id").toString().left(12),state);
    const auto method=backend.captureConfig.backend==aero::DdcBackend::HelperX86?QString("32-bit helper"):QString("Native x64");
    const auto mode=backend.captureConfig.receiveMode==aero::DdcReceiveMode::MTI?QString("MTI"):backend.captureConfig.receiveMode==aero::DdcReceiveMode::LegacyMT?QString("classic MT"):QString("automatic MTI / MT");
    text+="\nSelected method: "+method+" · "+mode;
    if(!error.isEmpty())text+="\n"+error;
    if(backend.captureConfig.connectionPolicy==aero::DdcConnectionPolicy::Compatibility)text+="\nCompatibility connection: physical qualification NOT RUN.";
    const bool failure=backend.captureConfig.hardware&&state=="failed";
    const bool showActions=backend.captureConfig.hardware&&(failure||!backend.acquisition.connected());
    for(auto button:{detailsButton,copy,chooseMethod,collect,manager})button->setVisible(showActions);
    for(int i=0;i<recovery->count();++i)
      if(auto widget=recovery->itemAt(i)->widget())widget->setVisible(failure);
    summary->setVisible(failure);
    if(summary->text()!=text)summary->setText(text);
    const auto reasonText=recordingDisabledReason();
    reason->setVisible(!reasonText.isEmpty()&&(backend.captureConfig.hardware||savingSession));
    if(reason->text()!=reasonText)reason->setText(reasonText);
    busy->setVisible(animate&&(connecting||connectAfterDiscovery));
    compatibility->setEnabled(idle&&!discoveryRunning);manager->setEnabled(idle&&!discoveryRunning);
    for(unsigned c=0;c<2;++c){partial[c]->setVisible(failure&&m.configuredChannels[c]&&(backend.captureConfig.enabled[c*2]||backend.captureConfig.enabled[c*2+1]));partial[c]->setEnabled(idle);}
  };refresh();connect(&tick,&QTimer::timeout,panel,refresh);return panel;
}
void Window::configurationChanged() {
  configurationRetry.stop();
  if(!connecting && !backend.acquisition.active() && backend.acquisition.connected()) backend.acquisition.stop();
  refreshOperatorState();
}
void Window::dragEnterEvent(QDragEnterEvent *event) {
  if(!unlocked || !event->mimeData()->hasUrls() || event->mimeData()->urls().size()!=1) return;
  const auto url=event->mimeData()->urls().front();
  const auto suffix=QFileInfo(url.toLocalFile()).suffix().toLower();
  if(url.isLocalFile() && (suffix=="json" || suffix=="csv")) event->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *event) {
  if(!unlocked || !event->mimeData()->hasUrls() || event->mimeData()->urls().size()!=1) return;
  const auto url=event->mimeData()->urls().front();const auto suffix=QFileInfo(url.toLocalFile()).suffix().toLower();
  if(!url.isLocalFile() || (suffix!="json" && suffix!="csv")) return;
  event->acceptProposedAction();importIcdPath(url.toLocalFile());
}

void Window::finishConnection() {
  if(closingCapture || !connecting || connectionFuture.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
  const bool ok=connectionFuture.get();connecting=false;
  finishConnectionEvidence(ok&&!automaticHeld,automaticHeld?QString("Connection cancelled"):QString::fromStdString(backend.acquisition.metrics(false).error));
  if(ok && !automaticHeld) {
    if(connectionIsRecording) {
      viewingReplayStatistics=false;rtDisplayPaused=false;
      if(auto error=desktop::describeSession(currentFile,backend.captureConfig,settings.value("nextRunName").toString(),desktop::csvOptions(settings));!error.isEmpty())toast(error);
      preserveSessionConnection(currentFile);
      backend.archive.clear();backend.archiveExample=!backend.captureConfig.hardware;backend.replaySource=false;backend.replayPlaying=false;backend.selected={};
      model.records.clear();selectedMonitorId=0;paneSelection={};model.refresh();refreshMonitorPanes();captureClock.restart();
      eventText="Recording started";
    } else {eventText="Devices connected — press Start recording";navigate(4);}
  } else {
    backend.acquisition.stop();
    eventText=automaticHeld ? "Connection cancelled" : QString::fromStdString(backend.acquisition.metrics(false).error);
    toast(eventText);
  }
  refreshOperatorState();
}

void Window::refreshOperatorState() {
  auto simBadge=findChild<QLabel *>("simulationBadge");
  simBadge->setText(backend.captureConfig.hardware?"DDC · RECEIVE ONLY":"SIMULATOR");
  simBadge->setVisible(page!=4 && width()>=1060);
  if(!captureButton) return;
  refreshWorkflowStepper();
  const auto metrics=backend.acquisition.metrics(false);
  captureButton->setVisible(page!=2);
  captureButton->setEnabled(!savingSession&&!closingCapture&&!metrics.cleanupBlocked);
  captureButton->setToolTip(metrics.cleanupBlocked ? QString::fromStdString(metrics.error) : QString());
  captureButton->setText(metrics.cleanupBlocked ? "Connection blocked" : savingSession ? "Saving…" : connectAfterDiscovery ? "Checking assignments…" : connecting ? "Cancel connection" : backend.acquisition.connected() || backend.acquisition.active() ? "Disconnect devices" : !backend.captureConfig.hardware ? "Start simulator" : metrics.failed ? "Retry connection" : "Connect devices");
  desktop::actionIcon(captureButton,(!backend.captureConfig.hardware && !backend.acquisition.connected() && !backend.acquisition.active())?"play":"link",true);
  stateBadge->setText(savingSession?"Saving recording":connecting||connectAfterDiscovery?"Checking devices":metrics.failed?
    (currentFile.isEmpty()?"Connection failed":"Recording incomplete"):backend.acquisition.active()?"● Recording":backend.acquisition.connected()?"● Connected":"● Disconnected");
  const auto detail=QString("CH1 A/B %1/%2 · CH2 A/B %3/%4 · %5 checkpointed · %6 display skips")
    .arg(metrics.sourceCounts[0]).arg(metrics.sourceCounts[1]).arg(metrics.sourceCounts[2]).arg(metrics.sourceCounts[3])
    .arg(metrics.checkpointed).arg(metrics.displayDrops);
  if(captureSummary) {
    const auto folder=QFileInfo(currentFile).absolutePath();
    const auto summary=detail+" · "+QFontMetrics(captureSummary->font()).elidedText(folder,Qt::ElideMiddle,170);
    if(captureSummary->text()!=summary)captureSummary->setText(summary);
    captureSummary->setToolTip(folder+QString("\nHardware loss reported %1 · overflow %2 · live analysis skips %3\nWriter peak %4 MiB")
      .arg(metrics.hardwareLost).arg(metrics.hardwareOverflows).arg(metrics.analysisSkipped)
      .arg(double(metrics.writerQueueHighWater)/(1024*1024),0,'f',1)+
      (metrics.error.empty()?QString():"\n"+QString::fromStdString(metrics.error)));
    captureSummary->setVisible(!currentFile.isEmpty()&&(backend.acquisition.active()||savingSession||metrics.failed));
  }
  const auto color=metrics.failed ? "#b33a32" : backend.acquisition.active() && backend.captureConfig.hardware ? "#28624d" : "#5b6975";
  const auto badgeStyle=QString("color:%1;background:#eef3f7;border:1px solid #d5e0e8;border-radius:14px;font-size:13px;font-weight:600;padding:4px 12px;").arg(color);
  if(stateBadge->styleSheet()!=badgeStyle)stateBadge->setStyleSheet(badgeStyle);
  for(unsigned i=0;i<2;++i) if(deviceStatuses[i]) {
    if(deviceTitles[i]) deviceTitles[i]->setText(QString("Device %1  /  %2").arg(backend.captureConfig.logicalDevices[i]).arg(i ? "Internal" : "External"));
    const auto &device=metrics.devices[i];QString status,ink="#62685f";
    const auto lastTraffic=std::max(metrics.lastTrafficNs[i*2],metrics.lastTrafficNs[i*2+1]);
    const auto nowNs=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
    switch(device.phase) {
      case aero::DevicePhase::Checking: status="Checking configuration…";break;
      case aero::DevicePhase::Ready: status="Connected · ready to record";break;
      case aero::DevicePhase::Monitoring: status=lastTraffic && nowNs-lastTraffic<2000000000ull?"Monitoring":"Monitoring · waiting for bus traffic";ink="#28624d";break;
      case aero::DevicePhase::Simulating: status="Simulator · hardware not connected";ink="#8a611f";break;
      case aero::DevicePhase::Failed: status="Not ready · check device setup";ink="#b33a32";break;
      case aero::DevicePhase::Disabled: status="Disabled";break;
      default: status=device.message.empty()?QString("Not connected"):QString::fromStdString(device.message);break;
    }
    QString buses;
    if(backend.captureConfig.enabled[i*2]) buses="A";
    if(backend.captureConfig.enabled[i*2+1]) buses+=(buses.isEmpty() ? "B" : " + B");
    const auto subtitle=QString("Channel %1 · %2").arg(i+1).arg(buses.isEmpty()?"No buses":buses);
    if(deviceStatuses[i]->text()!=subtitle)deviceStatuses[i]->setText(subtitle);
    if(auto badge=deviceStatuses[i]->parentWidget()->findChild<QLabel *>(QString("deviceBadge%1").arg(i))) {
      const auto label=QString("●  %1").arg(status);
      if(badge->text()!=label)badge->setText(label);
      const auto style=QString("color:%1;").arg(ink);
      if(badge->styleSheet()!=style)badge->setStyleSheet(style);
    }
    const auto deviceStyle=QString("color:%1;font-size:13px;").arg(ink);
    if(deviceStatuses[i]->styleSheet()!=deviceStyle)deviceStatuses[i]->setStyleSheet(deviceStyle);
    deviceStatuses[i]->setToolTip(QString::fromStdString(device.message));
  }
  for(unsigned c=0;c<2;++c)if(paneCounts[c]) {
    const auto &device=metrics.devices[c];const auto count=metrics.sourceCounts[c*2]+metrics.sourceCounts[c*2+1];
    const QString state=connecting?"Checking":device.phase==aero::DevicePhase::Failed?"Connection failed":device.phase==aero::DevicePhase::Disabled?"Disabled":backend.acquisition.active()?(backend.captureConfig.hardware?(count?"Receiving":"Monitoring / no traffic"):"Simulation"):backend.acquisition.connected()?"Connected · ready to record":device.message.empty()?QString("Stopped"):QString::fromStdString(device.message);
    const auto counts=rtGrouped?rtMonitorSummary(c,state):
      QString("%1 · CH%2 / logical %3 · %4\n%5 recorded · %6 shown").arg(c?"Internal":"External").arg(c+1).arg(backend.captureConfig.logicalDevices[c]).arg(state).arg(count).arg(paneModels[c].rowCount());
    if(paneCounts[c]->text()!=counts)paneCounts[c]->setText(counts);
    paneCounts[c]->setToolTip(QString::fromStdString(device.message)+"\nChannel totals count each raw message once; RT-to-RT participates in both RT rows. Filters and display pause do not change recorded counts.");
  }
  if(recordButton) {
    const bool recording=backend.acquisition.active();
    // Keep the single primary capture action available while recording. The
    // merged Device Setup card owns preparation fields; the monitor retains
    // only this compact start/stop control.
    recordButton->setVisible(true);
    recordButton->setEnabled(!connecting&&!savingSession&&!closingCapture&&
      (recording||!backend.captureConfig.hardware||backend.acquisition.connected()));
    recordButton->setText(savingSession?"Saving…":recording?"Stop && save":"Start recording");
  }
  if(monitorCsv){
    // Check the file when saving. A storage request here can block every
    // monitor refresh while Windows is flushing the recording volume.
    monitorCsv->setEnabled(!savingSession && !connecting && !currentFile.isEmpty());
    monitorCsv->setText(savingSession?"Saving…":backend.acquisition.active()?"Stop && save":"Save / retry CSVs");
  }
  if(selectedSummary) {
    const auto record=selectedMonitorRecord();
    if(record) {const auto p=aero::interpret(*record);selectedSummary->setText(QString("#%1  ·  CH %2 / %3  ·  Payload RT %4 / SA %5\n%6").arg(record->id).arg(record->channel).arg(record->bus ? "B" : "A").arg(p.payloadCommand.rt).arg(p.payloadCommand.sa).arg(words(*record).left(115)));selectedSummary->setToolTip(QString::fromStdString(aero::protocolDiagnostics(*record,p))+"\n"+QString::fromStdString(aero::statusDescription(p)));}
    else selectedSummary->setText("Select a record to inspect its words, engineering values or plot.");
  }
}

bool Window::nativeEvent(const QByteArray &,void *message,qintptr *) {
#ifdef Q_OS_WIN
  const auto event=static_cast<MSG *>(message);
  if(event->message==WM_DEVICECHANGE && (event->wParam==DBT_DEVICEARRIVAL || event->wParam==DBT_DEVNODES_CHANGED))
    QTimer::singleShot(1500,this,[this]{automaticCapture();});
#else
  Q_UNUSED(message);
#endif
  return false;
}

void Window::exportMonitorCsv() {
  for(auto watcher:findChildren<QFutureWatcherBase *>()) if(watcher->isRunning()) {toast("An export is already running.");return;}
  if(analysisData().empty()) {toast("No captured records to export.");return;}
  const auto directory=settings.value("exportFolder",sessionDir+"/Exports").toString();
  if(!QDir().mkpath(directory)) {toast("Cannot create export folder.");return;}
  const auto path=QDir(directory).filePath("Monitor_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::Id128).left(8)+".csv");
  auto watcher=new QFutureWatcher<aero::ExportResult>(this);
  connect(watcher,&QFutureWatcher<aero::ExportResult>::finished,this,[this,watcher,path] {
    const auto result=watcher->result();applicationEvent("monitor_export_finished",path,{{"records",double(result.count)},{"error",QString::fromStdString(result.error)}});toast(result.error.empty() ? "Exported "+QString::number(result.count)+" records: "+path : QString::fromStdString(result.error));watcher->deleteLater();
  });
  const auto bus=model.busFilter,search=model.search;const auto channel=model.channelFilter,rt=model.rtFilter;
  toast("Exporting selected channel and display filters…");
  watcher->setFuture(QtConcurrent::run([records=analysisData(),mappings=backend.mappings(),path,bus,search,channel,rt,options=desktop::csvOptions(settings)] {
    aero::Workbench job;job.profile=mappings;
    for(const auto &record:records) {
      const auto command=aero::command(record.commandWord);
      if(channel && record.channel!=channel) continue;
      if(!bus.isEmpty() && bus!=(record.bus ? "B" : "A")) continue;
      if(rt>=0 && command.rt!=rt) continue;
      if(!search.isEmpty() && !words(record).contains(search,Qt::CaseInsensitive) && !hexword(record.commandWord).contains(search,Qt::CaseInsensitive)) continue;
      job.archive.push_back(record);
    }
    aero::ExportResult result;result.count=job.archive.size();
    QByteArray bytes=QByteArray::fromHex("efbbbf")+QByteArray::fromStdString(aero::sessionCsvHeader(options));std::array<std::optional<std::uint64_t>,2> previous;
    for(const auto &r:job.archive){bytes+=QByteArray::fromStdString(aero::sessionCsvRecord(r,previous[r.channel-1],options));previous[r.channel-1]=r.timestampNs;}
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) {result.error="CSV export failed";return result;}
    QSaveFile checksum(path+".sha256");const auto digest=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
    if(!checksum.open(QIODevice::WriteOnly) || checksum.write(digest)!=digest.size() || !checksum.commit()) result.error="CSV saved; checksum could not be saved";
    QSaveFile profile(path+".profile.json");const auto json=QJsonDocument(desktop::profileToJson(mappings)).toJson();
    if(!profile.open(QIODevice::WriteOnly) || profile.write(json)!=json.size() || !profile.commit()) result.error="CSV saved; profile could not be saved";
    QSaveFile manifest(path+".export.json");const auto metadata=QJsonDocument(QJsonObject{{"schema","aerotrace.filtered-csv.v1"},{"layout",options.layout==aero::CsvLayout::Compact11?"compact11":"legacy42"},{"radix",int(options.radix)},{"scope","Retained records after selected display filters"},{"rows",double(result.count)},{"time_origin","Relative card time; zero-based elapsed days"}}).toJson();
    if(!manifest.open(QIODevice::WriteOnly)||manifest.write(metadata)!=metadata.size()||!manifest.commit())result.error="CSV saved; export options could not be saved";
    result.text.clear();return result;
  }));
}

bool Window::operatorWorkflowTest(const QString &directory) {
  QDir().mkpath(directory);QStringList results;bool passed=true;
  QMap<QString,QVariant> original;for(const auto &key:settings.allKeys()) original[key]=settings.value(key);
  settings.setValue("monitorLayout","tabs");settings.setValue("monitorRowMode","raw");
  resize(1500,940);setSidebarCollapsed(false);
  auto check=[&](bool ok,const QString &message){passed&=ok;results<<(ok ? "PASS: " : "FAIL: ")+message;};
  unlocked=true;backend.captureConfig.hardware=false;backend.archive.clear();model.records.clear();
  backend.captureConfig.enabled={true,true,true,true};findChild<QLabel *>("simulationBadge")->setText("SIMULATOR");
  for(int i=0;i<80;++i) {auto record=aero::synthetic(i);model.records.push_back(record);backend.archive.push_back(record);}model.refresh();navigate(4);
  QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();const auto expandedTable=monitor->width();
  setSidebarCollapsed(true);QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();
  check(sidebar->width()==66 && nav[4]->text().isEmpty() && !nav[4]->icon().isNull() && !nav[4]->accessibleName().isEmpty() && !recoveryNavigation->accessibleName().isEmpty() && monitor->width()>expandedTable+100,"Collapsed navigation retains accessible icons and enlarges the table");
  setSidebarCollapsed(false);check(!nav[4]->text().isEmpty(),"Navigation expands with labels");
  deviceTabs->setCurrentIndex(1);check(model.rowCount()>0 && std::all_of(model.visible.begin(),model.visible.end(),[&](int i){return model.records[i].channel==1;}),"Device 0 tab isolates Channel 1 / External");
  deviceTabs->setCurrentIndex(2);check(model.rowCount()>0 && std::all_of(model.visible.begin(),model.visible.end(),[&](int i){return model.records[i].channel==2;}),"Device 1 tab isolates Channel 2 / Internal");
  findChild<QPushButton *>("busBButton")->click();check(model.rowCount()>0 && std::all_of(model.visible.begin(),model.visible.end(),[&](int i){return model.records[i].channel==2 && model.records[i].bus==1;}),"Bus filter preserves the selected device");
  findChild<QPushButton *>("allBusButton")->click();deviceTabs->setCurrentIndex(0);
  check(model.rowCount()==80,"Both channels restores all retained records");
  check(monitor->viewport()->height()>300,QString("Monitor receives the main workspace height (%1 px)").arg(monitor->viewport()->height()));
  const auto connectionSummary=content->findChild<QLabel *>("connectionResultSummary");
  check((!connectionSummary||!connectionSummary->isVisible()) &&
        !content->findChild<QWidget *>("recordingSetup"),
        "Simulator workspace keeps stored DDC failures out of the live monitor setup area");
  settings.setValue("exportFolder",directory);exportMonitorCsv();
  QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<10000) {QApplication::processEvents();bool running=false;for(auto watcher:findChildren<QFutureWatcherBase *>()) running|=watcher->isRunning();if(!running) break;QThread::msleep(5);}
  check(!QDir(directory).entryList({"Monitor_*.csv"},QDir::Files).isEmpty(),"One-click CSV writes a real export");
  monitor->selectRow(6);QApplication::processEvents();grab().save(directory+"/monitor-expanded.png");
  setSidebarCollapsed(true);QApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QApplication::processEvents();grab().save(directory+"/monitor-collapsed.png");setSidebarCollapsed(false);
  const auto previousSessionDir=sessionDir;QTemporaryDir captureDirectory;sessionDir=captureDirectory.path();
  for(int ch=1;ch<=2;++ch) for(const auto bus:{"A","B"}) settings.setValue(QString("channel%1Bus %2").arg(ch).arg(bus),true);
  backend.captureConfig.simulatorRate=1000;startCapture();navigate(4);
  QEventLoop first;QTimer::singleShot(250,&first,&QEventLoop::quit);first.exec();monitor->selectRow(0);
  const auto selected=selectedMonitorId;
  QEventLoop later;QTimer::singleShot(300,&later,&QEventLoop::quit);later.exec();
  check(selected && monitor->currentIndex().isValid() && model.at(monitor->currentIndex().row())->id==selected,"Live batches preserve the operator's selected record");
  backend.stopRecording();sessionDir=previousSessionDir;
  automaticHeld=true;configurationRetry.stop();configurationChanged();automaticCapture();
  check(automaticHeld && !configurationRetry.isActive() && !backend.acquisition.active(),"Stop holds capture stopped across configuration and arrival retries");
  automaticHeld=false;
  auto target=aero::synthetic(0);target.id=1001;target.channel=2;target.bus=1;target.timestampNs=1234567890;target.words[0]=2000;
  auto other=target;other.id=1002;other.channel=1;other.bus=0;
  backend.applyProfile(aero::Workbench::demoSignals());backend.archive={other,target};model.records=backend.archive;model.refresh();navigate(4);
  monitor->setColumnHidden(17,false);monitor->setCurrentIndex(model.index(1,17));plotMonitorSelection();
  check(page==7 && analysisChannel==2 && analysisBus==1 && plotRawColumn<0 && !plots.empty() && plots.front()->pointCount()==1,"Word selection plots the mapped engineering field on its own channel/bus clock");
  plots.front()->onPick(QPointF(target.timestampNs/1e9,20));check(backend.selected.id==target.id,"Plot cursor resolves the original record on the selected stream");
  auto evicted=target;evicted.id=1003;evicted.timestampNs+=1000000000;backend.archive={evicted};
  plots.front()->onPick(QPointF(target.timestampNs/1e9,20));check(backend.selected.id==0,"An evicted plotted record cannot resolve to unrelated newer raw data");backend.archive={other,target};
  navigate(4);monitor->setColumnHidden(9,false);monitor->setCurrentIndex(model.index(1,9));plotMonitorSelection();
  check(page==7 && plotRawColumn==9 && plots.front()->pointCount()==1,"An unmapped numeric column plots raw values against card time");
  // Use disconnected hardware evidence and synthetic mappings for comparable
  // renders. This does not scan Windows or exercise a physical DDC device.
  backend.acquisition.stop();
  backend.captureConfig.hardware=true;
  currentFile.clear();
  sessionSaveState.clear();
  connectionAttempt={};
  hardwareDiscovery={{"device_enumeration_complete",true},{"physical_presence","absent"},{"detected_devices",0}};
  backend.archive.clear();
  model.records.clear();
  model.refresh();
  settings.setValue("reduceMotion",true);
  settings.setValue("monitorLayout","tabs");
  settings.setValue("monitorRowMode","raw");
  settings.setValue("monitorTab",2);
  settings.setValue("csvLayout","legacy42");
  settings.setValue("csvRadix",10);
  settings.setValue("nextRunName","run10");
  settings.setValue("dense",true);
  settings.setValue("exportFolder",QDir::toNativeSeparators(QDir::tempPath()+"/AeroTrace/Exports"));
  statusBar()->clearMessage();
  settings.setValue("ddcAssignmentMode","manual");
  const bool compact=qEnvironmentVariableIntValue("AEROTRACE_REFERENCE_COMPACT")>0;
  resize(compact?1366:1672,compact?720:941);
  const auto settle=[] {
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
    QApplication::processEvents();
  };
  const auto screenshot=[&](const QString &name) {
    settle();
    check(grab().save(directory+"/reference-"+name+".png"),"Rendered reference state: "+name);
  };
  navigate(4);
  settle();
  check(recordButton&&!recordButton->isEnabled()&&content->findChild<QWidget *>("emptyMonitorNotice")->isVisible(),
        "Disconnected monitor shows its empty state and blocks recording");
  check(sidebar->width()==(compact?220:278),"Sidebar follows the reference and compact widths");
  const auto iconButtons=[&](QWidget *root,const QStringList &names,const QString &context) {
    QStringList missing;
    for(const auto &name:names) {
      const auto button=root->findChild<QPushButton *>(name);
      if(!button || button->icon().pixmap(QSize(16,16)).isNull() || (button->text().isEmpty()&&button->accessibleName().isEmpty()))
        missing<<name;
    }
    check(missing.isEmpty(),context+(missing.isEmpty()?QString():": "+missing.join(", ")));
  };
  iconButtons(content,{"columnsButton","allBusButton","busAButton","busBButton","tileMonitorWindows","cascadeMonitorWindows"},
              "Monitor filters and layout actions have labelled icons");
  const auto search=content->findChild<QLineEdit *>("hexSearch");
  check(search&&search->actions().size()==1&&!search->actions().front()->icon().isNull(),
        "Monitor search has one search icon");
  screenshot("monitor");
  check(!findChild<QPushButton *>("moreTools")&&!findChild<QPushButton *>("assistantNavigation")&&!nav[15]&&csvNavigation->isVisible()&&
    sidebar->isAncestorOf(recoveryNavigation),"Every tool has a direct sidebar destination");
  iconButtons(sidebar,{"csvNavigation","recoveryNavigation"},"Capture destinations keep labelled icons");
  auto focus=content->findChild<QPushButton *>("monitorFocusMode");
  const int previousHeight=monitor->height();
  if(focus)focus->click();
  settle();
  check(focus&&monitorFocused&&monitor->height()>previousHeight&&deviceTabs->currentIndex()==2&&
        !content->findChild<QWidget *>("recordingSetup"),"Focus enlarges the table and preserves its selected channel");
  if(focus)focus->click();
  settle();
  const auto tableGeometry=monitor->geometry();
  check(!content->findChild<QWidget *>("recordingSetup"),
        "Recording setup is kept out of the live monitor canvas");

  navigate(2);
  settle();
  scroll->verticalScrollBar()->setValue(0);
  screenshot("device-discovery");
  check(content->findChild<QWidget *>("emptyDeviceNotice")->isVisible(),
        "Completed zero-device discovery has an illustrated empty state");
  scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
  screenshot("device-checks");
  check(content->findChild<QWidget *>("connectionChecks")->findChildren<QLabel *>().size()==36,
        "Connection checks use structured rows with status icons");

  std::vector<aero::Signal> referenceSignals;
  for(int group=1;group<=4;++group) {
    for(int field=1;field<=18;++field) {
      aero::Signal signal;
      signal.id="Fixture "+std::to_string(group)+":"+std::to_string(field);
      const bool floating=field<=3;
      signal.word=unsigned(floating?2*(field-1):field+2);
      signal.sourceSignalId="Demo word "+std::to_string(signal.word+1);
      if(floating)signal.sourceSignalId+="–"+std::to_string(signal.word+2);
      if(group<4)signal.subsystem="Subsystem-"+std::to_string(group);
      signal.messageId=group<4?"04102":"";
      signal.width=floating?32:16;
      signal.numericType=floating?"float32":"integer";
      signal.unit=floating?"km/hr":"degree";
      signal.channel=group==4?0:group==3?2:1;
      signal.rt=unsigned(group+2);
      signal.sa=12;
      referenceSignals.push_back(std::move(signal));
    }
  }
  const auto profileError=backend.applyProfile(std::move(referenceSignals));
  check(profileError.empty(),"Synthetic mapping fixture validates through the native profile");
  workspacePdf=property("operatorReferencePdf").toString();
  workspaceDocument={{"document",workspacePdf.isEmpty()?"Example ICD":QFileInfo(workspacePdf).fileName()}};
  workspaceSubsystem="Subsystem-1";
  workspaceScopeKey="live";
  analysisChannel=1;
  analysisBus=0;
  navigate(12);
  screenshot("icd");
  auto mappingTree=content->findChild<QTreeWidget *>("manualCommands");
  check(mappingTree!=nullptr && content->findChild<QWidget *>("manualMappingHost")!=nullptr,
        "Signal Mappings renders directly in Step 3 without modal PDF library wrapper");
  iconButtons(content,{"importIcdButton","exportProfileButton","nextToEngineeringButton"},
              "Signal Mappings actions have labelled icons");
  check(workflowStepper && workflowStepper->isVisible(),
        "Guided Workflow stepper bar is visible across active steps");
  navigate(16);
  screenshot("preferences");
  check(nav[16]->isChecked()&&!content->findChild<QPushButton *>("saveButton")->icon().isNull(),
        "Preferences uses the direct sidebar route and a labelled Save action");
  check(!findChild<QPushButton *>("alertsButton") && !findChild<QPushButton *>("icdChatButton") &&
        !findChild<QPushButton *>("assistantNavigation") && nav[2]->isVisible() && nav[16]->isVisible(),
        QString("Retired Bus Analysis and ICD Assistant entry points are absent from the operator shell (Device Setup=%1, Preferences=%2)")
          .arg(nav[2]->isVisible()).arg(nav[16]->isVisible()));

  settings.clear();for(auto i=original.begin();i!=original.end();++i) settings.setValue(i.key(),i.value());settings.sync();
  QFile report(directory+"/operator-workflow.txt");if(!report.open(QIODevice::WriteOnly)) return false;report.write(results.join('\n').toUtf8());return passed;
}
