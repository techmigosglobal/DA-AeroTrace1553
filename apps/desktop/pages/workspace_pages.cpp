#include "main_window.hpp"
#include "services/csv_conversion.hpp"
#include "services/session_store.hpp"
#include "services/runtime_paths.hpp"
#include "services/ddc_discovery.hpp"
#include "services/workspace_style.hpp"
#include "services/operator_style.hpp"
#include "widgets/startup_splash.hpp"
#include "helper_packs.hpp"
#include "ui_ddc_method.h"
namespace {
class RtCard : public QFrame {
public:
  QPushButton *selector{};
protected:
  void mouseReleaseEvent(QMouseEvent *event) override {
    if(selector && event->button()==Qt::LeftButton && rect().contains(event->position().toPoint()))
      selector->click();
    QFrame::mouseReleaseEvent(event);
  }
};
class RtGrid : public QFrame {
public:
  QGridLayout *grid=new QGridLayout(this);QList<QWidget *> cards;int columns{8};
  RtGrid(){grid->setSizeConstraint(QLayout::SetNoConstraint);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);}
  QSize minimumSizeHint() const override {return {280,grid->minimumSize().height()};}
  QSize sizeHint() const override {return {1000,grid->sizeHint().height()};}
  void arrange(){int count=qBound(2,width()/145,8);if(count==columns&&!cards.isEmpty()&&grid->count()==cards.size())return;columns=count;for(auto card:cards)grid->removeWidget(card);for(int i=0;i<cards.size();++i)grid->addWidget(cards[i],i/columns,i%columns);updateGeometry();}
  void resizeEvent(QResizeEvent *event) override {QFrame::resizeEvent(event);arrange();}
};
QFrame *surface(QLayout *layout) {auto f=new QFrame;f->setProperty("card",true);layout->addWidget(f);return f;}
QLabel *heading(const QString &text,QLayout *layout) {auto l=new QLabel(text);l->setProperty("tone","cardtitle");layout->addWidget(l);return l;}
}
QWidget *Window::saveControls(bool compact) {
  auto frame=new QFrame;
  frame->setObjectName("recordingSetup");
  frame->setProperty("card",!compact);
  auto layout=new QVBoxLayout(frame);
  if(compact)layout->setContentsMargins(0,0,0,0);
  else {
    desktop::addCardShadow(frame);
    layout->addWidget(connectionResultPanel(true));
  }
  auto row=new QHBoxLayout;layout->addLayout(row);
  auto folder=new QLineEdit(settings.value("exportFolder",desktop::defaultExportDirectory()).toString());folder->setObjectName("csvFolderEdit");folder->setAccessibleName("Recording output folder");
  auto name=new QLineEdit(settings.value("nextRunName",desktop::suggestedRunName(folder->text())).toString());name->setObjectName("runNameEdit");name->setAccessibleName("Run name");name->setMaximumWidth(240);
  csvFolderEdit=folder;runNameEdit=name;
  auto browse=new QPushButton("Browse…");browse->setObjectName("browseCsvFolder");desktop::actionIcon(browse,"folder");
  if(compact){name->setMaximumWidth(QWIDGETSIZE_MAX);row->addWidget(new QLabel("Run name"));row->addWidget(name,1);auto folderRow=new QHBoxLayout;layout->addLayout(folderRow);folderRow->addWidget(new QLabel("Output folder"));folderRow->addWidget(folder,1);folderRow->addWidget(browse);}
  else {row->addWidget(new QLabel("Output folder"));row->addWidget(folder,2);row->addWidget(browse);row->addWidget(new QLabel("Run name"));row->addWidget(name,1);}
  auto format=new QComboBox(frame);format->setObjectName("sessionCsvLayout");format->setAccessibleName("CSV column layout");format->addItem("11-column CSV","compact11");format->addItem("42-column CSV","legacy42");format->setCurrentIndex(settings.value("csvLayout","compact11")=="legacy42"?1:0);
  auto base=new QComboBox(frame);base->setObjectName("sessionCsvBase");base->setAccessibleName("CSV raw word base");base->addItem("Hexadecimal",16);base->addItem("Decimal",10);base->addItem("Binary",2);base->addItem("Octal",8);base->setCurrentIndex(qMax(0,base->findData(settings.value("csvRadix",16))));
  if(compact) {
    auto formatRow=new QHBoxLayout;
    formatRow->setSpacing(10);
    formatRow->addWidget(new QLabel("Export format"));
    formatRow->addWidget(format,1);
    formatRow->addWidget(new QLabel("Number base"));
    formatRow->addWidget(base,1);
    layout->addLayout(formatRow);
  }
  connect(format,&QComboBox::currentIndexChanged,frame,[this,format](int){settings.setValue("csvLayout",format->currentData());});connect(base,&QComboBox::currentIndexChanged,frame,[this,base](int){settings.setValue("csvRadix",base->currentData());});
  auto preview=new QLabel;
  preview->setObjectName("recordingOutputs");
  preview->setWordWrap(true);
  preview->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
  preview->setTextFormat(Qt::RichText);
  outputPreview=preview;
  layout->addWidget(preview);
  const auto refresh=[this,folder,name,preview]{settings.setValue("exportFolder",folder->text().trimmed());folder->setToolTip(folder->text());settings.setValue("nextRunName",name->text().trimmed());const auto baseName=name->text().toHtmlEscaped();
    const QString mark="<img src=':/icons/verified.svg' width='14' height='14'/> ";
    const QString gap="&nbsp;&nbsp; ";
    preview->setProperty("outputText","<b>Export Files</b>"+gap+mark+"Raw ATD"+gap+
      mark+"Integrity Report"+gap+mark+baseName+"_EXT.csv"+gap+mark+baseName+"_INT.csv");
    preview->setToolTip("Raw ATD, integrity report, "+name->text()+"_EXT.csv and "+name->text()+"_INT.csv");};
  connect(folder,&QLineEdit::textChanged,frame,refresh);connect(name,&QLineEdit::textChanged,frame,refresh);
  connect(browse,&QPushButton::clicked,frame,[this,folder]{const auto dir=QFileDialog::getExistingDirectory(this,"CSV output folder",folder->text());if(!dir.isEmpty())folder->setText(dir);});refresh();
  const auto lockInputs=[this,frame,folder,name,browse,preview,format,base]{
    const bool recording=backend.acquisition.active()||savingSession;
    if(auto shadow=frame->graphicsEffect();shadow && shadow->isEnabled()==recording)
      shadow->setEnabled(!recording);
    const bool enabled=!recording&&!connecting;
    for(auto widget:{static_cast<QWidget *>(folder),static_cast<QWidget *>(name),static_cast<QWidget *>(browse),static_cast<QWidget *>(format),static_cast<QWidget *>(base)})widget->setEnabled(enabled);const auto text=preview->property("outputText").toString()+
    (currentFile.isEmpty()?QString():"<br/>Raw file "+QFileInfo(currentFile).fileName().toHtmlEscaped())+
    (sessionSaveState.isEmpty()?QString():" · "+sessionSaveState.toHtmlEscaped());
    if(preview->text()!=text)preview->setText(text);};lockInputs();connect(&tick,&QTimer::timeout,frame,lockInputs);
  if(!compact) {
    auto save=new QPushButton("Stop && save");save->setObjectName("stopSaveButton");save->setProperty("primary",true);row->addWidget(save);monitorCsv=save;connect(save,&QPushButton::clicked,this,[this]{stopAndSave();});
    desktop::actionIcon(save,"save",true);
    auto actions=new QHBoxLayout;
    layout->removeWidget(preview);
    actions->addWidget(preview,1);
    actions->addWidget(new QLabel("Format"));
    actions->addWidget(format);
    actions->addWidget(base);
    layout->addLayout(actions);
  }
  return frame;
}
bool Window::chooseSaveDestination() {
  QDialog dialog(this);dialog.setWindowTitle("Save Internal and External CSVs");dialog.setMinimumWidth(620);auto layout=new QVBoxLayout(&dialog);
  auto note=new QLabel("The complete recording is retained for replay. Choose where to save both CSV files.");note->setWordWrap(true);layout->addWidget(note);
  auto form=new QFormLayout;layout->addLayout(form);QLineEdit folder(settings.value("exportFolder").toString()),name(settings.value("nextRunName").toString());folder.setObjectName("saveDialogFolder");name.setObjectName("saveDialogRunName");form->addRow("CSV folder",&folder);form->addRow("Run name",&name);
  auto shortcuts=new QHBoxLayout;layout->addLayout(shortcuts);
  for(const auto &item:QList<QPair<QString,QStandardPaths::StandardLocation>>{{"Desktop",QStandardPaths::DesktopLocation},{"Documents",QStandardPaths::DocumentsLocation}}) {auto button=new QPushButton(item.first);shortcuts->addWidget(button);connect(button,&QPushButton::clicked,&dialog,[&folder,item]{folder.setText(QStandardPaths::writableLocation(item.second));});}
  auto browse=new QPushButton("Browse…");shortcuts->addWidget(browse);connect(browse,&QPushButton::clicked,&dialog,[this,&folder]{auto path=QFileDialog::getExistingDirectory(this,"CSV folder",folder.text());if(!path.isEmpty())folder.setText(path);});
  auto error=new QLabel;error->setWordWrap(true);error->setStyleSheet("color:#a73d32");layout->addWidget(error);auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{auto problem=desktop::validateRunName(name.text().trimmed());if(folder.text().trimmed().isEmpty())problem="Choose a CSV folder.";if(!problem.isEmpty()){error->setText(problem);return;}dialog.accept();});
  if(dialog.exec()!=QDialog::Accepted)return false;
  if(csvFolderEdit)csvFolderEdit->setText(folder.text().trimmed());if(runNameEdit)runNameEdit->setText(name.text().trimmed());settings.setValue("exportFolder",folder.text().trimmed());settings.setValue("nextRunName",name.text().trimmed());return true;
}
void Window::welcome() {
  heading("Prepare a monitoring run",pageLayout);auto intro=new QLabel("Choose channels and an output folder before starting a recording.");intro->setWordWrap(true);pageLayout->addWidget(intro);
  auto row=new QHBoxLayout;pageLayout->addLayout(row);auto prepare=surface(row);row->setStretch(0,3);auto body=new QVBoxLayout(prepare);heading("Next run",body);body->addWidget(saveControls(true));
  auto buttons=new QHBoxLayout;body->addLayout(buttons);auto setup=new QPushButton("Review device setup");setup->setProperty("primary",true);auto open=new QPushButton("Open CSV viewer…");buttons->addWidget(setup);buttons->addWidget(open);connect(setup,&QPushButton::clicked,this,[this]{navigate(2);});connect(open,&QPushButton::clicked,this,[this]{csvTools();});
  auto readiness=surface(row);row->setStretch(1,2);auto ready=new QVBoxLayout(readiness);heading("Hardware readiness",ready);auto evidence=new QLabel; evidence->setWordWrap(true);ready->addWidget(evidence,1);auto details=new QPushButton("Device details");ready->addWidget(details);connect(details,&QPushButton::clicked,this,[this]{navigate(2);});
  const auto refresh=[this,evidence]{const auto metrics=backend.acquisition.metrics(false);evidence->setText(QString("Windows device: %1\n\nDriver / SDK: %2\n\nLogical channels: %3 / %4\n\nMonitor: %5").arg(hardwareDiscovery.isEmpty()?"Check in device setup":hardwareDiscovery.value("physical_presence").toString()=="unknown"?QString("Unknown — enumeration incomplete"):QString::number(hardwareDiscovery.value("detected_devices").toInt())+" detected").arg(hardwareDiscovery.isEmpty()?"Check in device setup":"See measured connection checks").arg(backend.captureConfig.logicalDevices[0]).arg(backend.captureConfig.logicalDevices[1]).arg(backend.acquisition.active()?"Recording":metrics.failed?"Connection failed":"Stopped"));};refresh();connect(&tick,&QTimer::timeout,evidence,refresh);
}
void Window::dashboard() {
  auto legend=new QLabel;rtLegend=legend;legend->setWordWrap(true);legend->setProperty("tone","notice");pageLayout->addWidget(legend);
  auto gridSurface=new RtGrid;gridSurface->setProperty("card",true);pageLayout->addWidget(gridSurface);auto grid=gridSurface->grid;grid->setSpacing(8);
  for(int rt=0;rt<32;++rt){auto card=new RtCard;card->setProperty("card",true);card->setProperty("rtIndex",rt);card->setMinimumHeight(125);auto body=new QVBoxLayout(card);body->setContentsMargins(12,12,12,12);body->setSpacing(8);auto select=new QPushButton(rt==31?"31 · Broadcast":QString("RT %1").arg(rt,2,10,QChar('0')));card->selector=select;select->setProperty("rtSelector",true);select->setFlat(true);body->addWidget(select);connect(select,&QPushButton::clicked,this,[this,rt]{selectedRt=rt;refreshRtDashboard();});
    for(int ch=0;ch<2;++ch){auto l=new QLabel;body->addWidget(l);if(rt<31)rtLights[ch][rt]=l;else l->setText(ch?"CH2 · Broadcast commands":"CH1 · Broadcast commands");}gridSurface->cards<<card;grid->addWidget(card,rt/8,rt%8);}
  rtDetail=new QLabel;rtDetail->setWordWrap(true);rtDetail->setProperty("tone","notice");pageLayout->addWidget(rtDetail);auto note=new QLabel("Passive observation reports responses seen during this run. Silent terminals cannot be identified as physically connected.");note->setWordWrap(true);note->setProperty("tone","muted");pageLayout->addWidget(note);refreshRtDashboard();
}
void Window::refreshRtDashboard() {
  if(!rtLegend)return;const auto metrics=backend.acquisition.metrics(false);const auto &activity=backend.replaySource?replayActivity:metrics.rtActivity;
  rtLegend->setText(QString("%1 · %2   |   ● Green: valid response observed this run   ○ Grey: not observed").arg(backend.replaySource?"Replay":backend.captureConfig.hardware?"Passive hardware monitoring":"SIMULATION").arg(backend.acquisition.active()?"Recording":metrics.failed?"Capture error — inspect diagnostics":"Stopped / recorded observations"));
  for(int ch=0;ch<2;++ch)for(int rt=0;rt<31;++rt)if(auto label=rtLights[ch][rt]){const auto &v=activity.channels[ch][rt];label->setText(QString("<span style='font-size:22px;color:%1'>●</span> <span style='font-size:12px'>CH%2 %3</span>").arg(v.responses?"#15803d":"#778797").arg(ch+1).arg(ch?"Internal":"External"));label->setProperty("responseObserved",v.responses>0);label->setAccessibleName(QString("RT %1, channel %2: %3").arg(rt).arg(ch+1).arg(v.responses?"valid response observed":"no response observed"));label->setToolTip(v.responses?QString("%1 valid responses · Bus %2 · last device time %3").arg(v.responses).arg(v.buses==3?"A + B":v.buses==1?"A":"B").arg(stamp(v.lastNs)):"No valid response observed during this run");}
  for(auto card:content->findChildren<QFrame *>())if(card->property("rtIndex").isValid()) {const int rt=card->property("rtIndex").toInt();desktop::workspaceState(card,"selectedRt",rt==selectedRt);desktop::workspaceState(card,"observedRt",rt<31&&(activity.channels[0][rt].responses||activity.channels[1][rt].responses));}
  if(selectedRt<0){rtDetail->setText("Select a remote terminal or Broadcast to see observations.");return;}
  if(selectedRt==31){rtDetail->setText(QString("Broadcast commands: CH1 %1 · CH2 %2. Broadcast address 31 does not return a terminal response.").arg(activity.broadcasts[0]).arg(activity.broadcasts[1]));return;}
  QStringList detail;for(int ch=0;ch<2;++ch){const auto &v=activity.channels[ch][selectedRt];detail<<QString("CH%1 %2: %3").arg(ch+1).arg(ch?"Internal":"External").arg(v.responses?QString("%1 responses · last device time %2 · Bus %3").arg(v.responses).arg(stamp(v.lastNs)).arg(v.buses==3?"A + B":v.buses==1?"A":"B"):"No response observed");}rtDetail->setText(QString("RT %1\n").arg(selectedRt,2,10,QChar('0'))+detail.join("\n"));
}
void Window::device() {
  auto wizard=new QStackedWidget(content);
  wizard->setObjectName("deviceWizard");
  pageLayout->addWidget(wizard,1);

  // Step 1: Recording Setup
  auto step1=new QWidget(wizard);
  auto step1Lay=new QVBoxLayout(step1);
  step1Lay->setContentsMargins(0,0,0,0);step1Lay->setSpacing(8);
  auto recordingCard=surface(step1Lay);
  auto recordingLayout=new QVBoxLayout(recordingCard);
  recordingLayout->setContentsMargins(14,10,14,12);recordingLayout->setSpacing(8);
  recordingLayout->addWidget(desktop::sectionHeading("Recording Setup","device",false,
    "Set run name, output location and export format."));
  recordingLayout->addWidget(saveControls(true));
  auto sourceRow=new QHBoxLayout;sourceRow->setSpacing(8);
  sourceRow->addWidget(new QLabel("Source"));
  auto source=new QComboBox;source->setObjectName("sourceMode");source->addItems({"DDC hardware · passive monitoring","Offline simulation · RT 03 and RT 08"});source->setCurrentIndex(backend.captureConfig.hardware?0:1);sourceRow->addWidget(source,1);
  auto scan=new QPushButton("Rescan DDC");scan->setObjectName("scanButton");desktop::actionIcon(scan,"search");sourceRow->addWidget(scan);
  recordingLayout->addLayout(sourceRow);
  auto step1Nav=new QHBoxLayout;
  auto backToAssign=new QPushButton("← Back: Device Assignment",step1);
  step1Nav->addWidget(backToAssign);
  step1Nav->addStretch(1);
  auto arm=new QPushButton;
  arm->setObjectName("armButton");
  arm->setProperty("primary",true);
  desktop::actionIcon(arm,"play",true);
  step1Nav->addWidget(arm);
  step1Lay->addLayout(step1Nav);
  step1Lay->addStretch(1);

  // Step 2: Device Assignment
  auto step2=new QWidget(wizard);
  auto step2Lay=new QVBoxLayout(step2);
  step2Lay->setContentsMargins(0,0,0,0);step2Lay->setSpacing(6);
  auto mainRow=new QHBoxLayout;mainRow->setSpacing(10);step2Lay->addLayout(mainRow,1);
  auto deviceCard=surface(mainRow);auto devices=new QVBoxLayout(deviceCard);
  devices->setContentsMargins(12,10,12,10);devices->setSpacing(6);
  devices->addWidget(desktop::sectionHeading("Windows Devices","device",false,
    "Detected DDC devices available on this system."));
  auto table=new QTableWidget;table->setObjectName("discoveredDdcDevices");table->setColumnCount(4);table->setHorizontalHeaderLabels({"Device","Hardware ID","Driver","PnP status"});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->verticalHeader()->hide();table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setMinimumHeight(110);devices->addWidget(table,1);
  auto empty=new desktop::TableNotice(table,"device-empty",46);
  empty->setObjectName("emptyDeviceNotice");
  auto deviceCount=new QLabel;
  deviceCount->setStyleSheet("background:#e6f4ed;color:#145331;border-radius:7px;padding:4px 10px;font-weight:600;font-size:13px;");
  devices->addWidget(deviceCount);
  const bool animate=desktop::startupMotionEnabled();
  auto scanProgress=new QProgressBar;
  scanProgress->setProperty("operatorBusy",true);
  scanProgress->setRange(0,0);
  scanProgress->setTextVisible(false);
  scanProgress->hide();
  devices->addWidget(scanProgress);
  auto manager=new QPushButton("Open DDC Card Manager");manager->setObjectName("openDdcCardManager");desktop::actionIcon(manager,"device");manager->setToolTip("Disconnect hardware before changing Card Manager assignments");devices->addWidget(manager);connect(manager,&QPushButton::clicked,this,&Window::openCardManager);
  auto mapping=new QLabel("Card Manager maintains the assignments. Select its existing board/channel entries here.");mapping->setWordWrap(true);mapping->setProperty("tone","muted");mapping->hide();devices->addWidget(mapping);
  auto channels=surface(mainRow);auto channelBody=new QVBoxLayout(channels);
  channelBody->setContentsMargins(12,10,12,10);channelBody->setSpacing(6);
  channelBody->addWidget(desktop::sectionHeading("Channel Assignments","sliders",false,
    "Assign logical device numbers and bus selections."));auto channelRow=new QHBoxLayout;channelBody->addLayout(channelRow,1);
  mainRow->setStretch(0,5);
  mainRow->setStretch(1,7);
  auto assignmentMode=new QComboBox;assignmentMode->setObjectName("ddcAssignmentMode");assignmentMode->addItems({"Use Card Manager assignments","Advanced manual logical numbers"});assignmentMode->setCurrentIndex(settings.value("ddcAssignmentMode","card_manager").toString()=="manual"?1:0);channelBody->insertWidget(1,assignmentMode);
  connect(assignmentMode,&QComboBox::currentIndexChanged,this,[this](int index){settings.setValue("ddcAssignmentMode",index?"manual":"card_manager");configurationChanged();});
  QList<QWidget *> controls{source,assignmentMode};
  for(int ch=0;ch<2;++ch){auto group=new QGroupBox(QString("Channel %1 / %2").arg(ch+1).arg(ch?"Internal":"External"));group->setProperty("channelRole",ch?"internal":"external");channelRow->addWidget(group);auto body=new QVBoxLayout(group);
    auto assigned=new QComboBox;assigned->setObjectName(QString("cardManagerChannel%1").arg(ch+1));assigned->setAccessibleName(QString("Card Manager assignment for channel %1").arg(ch+1));assigned->setMinimumContentsLength(20);assigned->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);body->addWidget(assigned);controls<<assigned;
    auto manualLabel=new QLabel("Advanced logical device number (SDK verifies access)");manualLabel->setWordWrap(true);body->addWidget(manualLabel);auto logical=new QSpinBox;logical->setObjectName(QString("logicalChannel%1").arg(ch+1));logical->setRange(0,31);logical->setValue(backend.captureConfig.logicalDevices[ch]);body->addWidget(logical);controls<<logical;
    connect(assigned,&QComboBox::currentIndexChanged,group,[this,ch,assigned,logical](int index){if(index<0||assigned->currentData().toInt()<0)return;logical->setValue(assigned->currentData().toInt());});
    const auto updateChoices=[this,ch,assigned,logical,manualLabel,assignmentMode]{const bool manual=assignmentMode->currentIndex()==1;assigned->setVisible(!manual);logical->setVisible(manual);manualLabel->setVisible(manual);const auto choices=desktop::ddcAssignmentChoices(hardwareDiscovery);const auto bytes=QJsonDocument(choices).toJson(QJsonDocument::Compact);
      if(assigned->property("assignmentChoices").toByteArray()==bytes)return;assigned->setProperty("assignmentChoices",bytes);QSignalBlocker blocked(assigned);assigned->clear();assigned->addItem("Choose existing board / physical channel…",-1);int selected=-1,automatic=-1,count=0;
      for(const auto &value:choices){const auto choice=value.toObject();const auto number=choice.value("logical_device").toInt();assigned->addItem(choice.value("label").toString(),number);if(number==backend.captureConfig.logicalDevices[ch])selected=assigned->count()-1;if(choice.value("physical_channel").toInt()==ch){automatic=assigned->count()-1;++count;}}
      if(settings.contains(QString("logicalChannel%1").arg(ch+1)))assigned->setCurrentIndex(selected>=0?selected:0);else assigned->setCurrentIndex(count==1?automatic:0);
    };updateChoices();connect(&tick,&QTimer::timeout,group,updateChoices);connect(assignmentMode,&QComboBox::currentIndexChanged,group,[updateChoices](int){updateChoices();});
    connect(logical,&QSpinBox::valueChanged,group,[this,ch](int value){backend.captureConfig.logicalDevices[ch]=value;settings.setValue(QString("logicalChannel%1").arg(ch+1),value);configurationChanged();});
    for(int bus=0;bus<2;++bus){auto check=new QCheckBox(bus?"Bus B":"Bus A");const auto key=QString("channel%1Bus %2").arg(ch+1).arg(bus?"B":"A");check->setObjectName(QString("channel%1Bus%2").arg(ch+1).arg(bus?"B":"A"));check->setChecked(settings.value(key,true).toBool());body->addWidget(check);controls<<check;connect(check,&QCheckBox::toggled,group,[this,key](bool on){settings.setValue(key,on);configurationChanged();});}body->addStretch();}
  auto step2Bottom=new QHBoxLayout;step2Bottom->setSpacing(10);step2Lay->addLayout(step2Bottom);
  auto simulationOptions=new QWidget;step2Bottom->addWidget(simulationOptions);auto simulationRow=new QHBoxLayout(simulationOptions);simulationRow->setContentsMargins(0,0,0,0);auto rateLabel=new QLabel("Simulated msgs/s");simulationRow->addWidget(rateLabel);auto rate=new QSpinBox;rate->setObjectName("simulatorRate");rate->setRange(1,100000);rate->setValue(backend.captureConfig.simulatorRate);simulationRow->addWidget(rate);rateLabel->setBuddy(rate);rate->setAccessibleName(rateLabel->text());rate->setToolTip("Total simulated messages across all enabled channels and buses.");controls<<rate;connect(rate,&QSpinBox::valueChanged,this,[this](int value){backend.captureConfig.simulatorRate=value;settings.setValue("simulatorRate",value);});
  auto methodWidget=new QWidget; Ui::DdcMethod methodForm; methodForm.setupUi(methodWidget); step2Bottom->addWidget(methodWidget,1);
  if(auto note=methodWidget->findChild<QLabel *>("methodNote"))note->hide();
  auto step2Nav=new QHBoxLayout;
  auto backToStep1=new QPushButton("← Back: Recording Setup",step2);
  backToStep1->hide();
  auto nextToChecks=new QPushButton("Next: Recording Setup →",step2);
  nextToChecks->setProperty("primary",true);
  step2Nav->addWidget(backToStep1);step2Nav->addStretch(1);step2Nav->addWidget(nextToChecks);
  step2Lay->addLayout(step2Nav);
  wizard->addWidget(step2);
  wizard->addWidget(step1);
  auto backendEdit=methodForm.ddcBackend; auto mode=methodForm.ddcReceiveMode; auto helper=methodForm.ddcHelperPack; auto chooseHelper=methodForm.chooseHelper; auto helperLabel=methodForm.helperLabel; desktop::actionIcon(chooseHelper,"file");
  backendEdit->setCurrentIndex(backend.captureConfig.backend==aero::DdcBackend::HelperX86?1:0); mode->setCurrentIndex(int(backend.captureConfig.receiveMode)); controls<<backendEdit<<mode<<helper<<chooseHelper;
  helper->addItem("Select a verified x86 helper pack…",QString());
  for(const auto &pack:aero::discoverDdcHelperPacks()){if(!pack.error.empty()||!pack.softwareVerified)continue;helper->addItem(QString::fromStdString(pack.id+" · "+pack.sdkIdentity),QString::fromStdWString(pack.manifest.wstring()));}
  const auto selectedHelper=QString::fromStdWString(backend.captureConfig.helperPack.wstring());if(!selectedHelper.isEmpty()&&helper->findData(selectedHelper)<0)helper->addItem(QFileInfo(selectedHelper).fileName(),selectedHelper);helper->setCurrentIndex(qMax(0,helper->findData(selectedHelper)));
  connect(backendEdit,&QComboBox::currentIndexChanged,this,[this,helper,chooseHelper,helperLabel](int value){backend.captureConfig.backend=value?aero::DdcBackend::HelperX86:aero::DdcBackend::NativeX64;settings.setValue("ddcBackend",value?"helper":"native");if(value){backend.captureConfig.connectionPolicy=aero::DdcConnectionPolicy::Compatibility;settings.setValue("ddcCompatibility",true);}helper->setVisible(value);chooseHelper->setVisible(value);helperLabel->setVisible(value);configurationChanged();});helper->setVisible(backendEdit->currentIndex()==1);chooseHelper->setVisible(backendEdit->currentIndex()==1);helperLabel->setVisible(backendEdit->currentIndex()==1);
  connect(mode,&QComboBox::currentIndexChanged,this,[this](int value){backend.captureConfig.receiveMode=static_cast<aero::DdcReceiveMode>(value);settings.setValue("ddcReceiveMode",value);configurationChanged();});
  connect(helper,&QComboBox::currentIndexChanged,this,[this,helper](int){const auto path=helper->currentData().toString();backend.captureConfig.helperPack=std::filesystem::path(path.toStdWString());settings.setValue("ddcHelperPack",path);configurationChanged();});
  connect(chooseHelper,&QPushButton::clicked,this,[this,helper]{const auto path=QFileDialog::getOpenFileName(this,"Select verified x86 helper pack",QCoreApplication::applicationDirPath()+"/helpers","Helper pack manifest (*.json)");if(path.isEmpty())return;const auto pack=aero::readDdcHelperPack(std::filesystem::path(path.toStdWString()));if(!pack.error.empty()||!pack.softwareVerified){toast("Helper pack rejected: "+QString::fromStdString(pack.error));return;}helper->addItem(QString::fromStdString(pack.id),path);helper->setCurrentIndex(helper->count()-1);});
  // Hidden Connection Checks backing widgets (screen removed per workflow)
  auto step3=new QWidget(content);
  step3->hide();
  auto step3Lay=new QVBoxLayout(step3);
  step3Lay->setContentsMargins(0,0,0,0);step3Lay->setSpacing(12);
  step3Lay->addWidget(connectionResultPanel());
  auto checkCard=surface(step3Lay);
  auto checks=new QVBoxLayout(checkCard);
  checks->setContentsMargins(16,14,16,16);checks->setSpacing(12);
  checks->addWidget(desktop::sectionHeading("Connection Checks","activity",false,
    "Verify driver, SDK, assignments and monitor status before capture."));
  auto checkBodyRow=new QHBoxLayout;checkBodyRow->setSpacing(16);
  checks->addLayout(checkBodyRow,1);
  auto summary=new QWidget;
  summary->setObjectName("connectionChecks");
  checkBodyRow->addWidget(summary,3);
  auto checkGrid=new QGridLayout(summary);
  checkGrid->setContentsMargins(8,0,8,0);
  checkGrid->setHorizontalSpacing(12);
  checkGrid->setVerticalSpacing(10);
  checkGrid->setColumnStretch(3,1);
  const QStringList checkNames{"Windows device","Driver binding","SDK runtime","Logical assignment",
    "Monitor CH1","Monitor CH2","Detected runtime","Selected pack","Receive mode"};
  const QStringList checkIcons{"device","layers","database","link","activity","activity","info","layers","download"};
  std::array<QLabel *,9> checkTexts{},checkStatuses{};
  for(int row=0;row<checkNames.size();++row) {
    auto icon=new QLabel;
    desktop::labelIcon(icon,checkIcons[row],17);
    checkGrid->addWidget(icon,row,0,Qt::AlignTop);
    auto key=new QLabel(checkNames[row]);
    key->setProperty("checkName",true);
    checkGrid->addWidget(key,row,1,Qt::AlignTop);
    checkStatuses[row]=new QLabel;
    checkGrid->addWidget(checkStatuses[row],row,2,Qt::AlignTop);
    auto value=new QLabel;
    value->setWordWrap(true);
    value->setTextFormat(Qt::PlainText);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    checkTexts[row]=value;
    checkGrid->addWidget(value,row,3);
  }
  auto rightInfo=new QVBoxLayout;rightInfo->setSpacing(10);
  auto explanation=new QLabel("The monitor interface is selected automatically during connection. No ICD is required for raw monitoring.");
  explanation->setWordWrap(true);
  explanation->setProperty("tone","notice");
  auto planeBox=new QLabel;planeBox->setAlignment(Qt::AlignCenter);planeBox->setMinimumHeight(90);
  desktop::labelIcon(planeBox,"aircraft",54);
  rightInfo->addWidget(explanation);
  rightInfo->addWidget(planeBox);
  rightInfo->addStretch(1);
  checkBodyRow->addLayout(rightInfo,2);
  auto detailsButton=new QPushButton("Technical Details");desktop::actionIcon(detailsButton,"settings");detailsButton->setCheckable(true);
  auto action=new QHBoxLayout;
  step3Lay->addLayout(action);
  action->addWidget(detailsButton);
  action->addStretch();
  auto backToStep2=new QPushButton("← Back: Recording Setup",step3);
  action->addWidget(backToStep2);
  auto nextStep=new QPushButton("Next: Monitor & Record →");
  nextStep->setObjectName("nextToMonitorButton");
  desktop::actionIcon(nextStep,"activity");
  nextStep->hide();
  action->addWidget(nextStep);
  connect(nextToChecks,&QPushButton::clicked,wizard,[wizard]{wizard->setCurrentIndex(1);});
  connect(backToAssign,&QPushButton::clicked,wizard,[wizard]{wizard->setCurrentIndex(0);});
  connect(nextStep,&QPushButton::clicked,this,[this]{navigate(4);});
  connect(arm,&QPushButton::clicked,this,[this]{if(backend.captureConfig.hardware)connectDevices();else startCapture();});
  auto details=new QWidget;auto detailLayout=new QVBoxLayout(details);auto runtime=new QLineEdit(QString::fromStdWString(backend.captureConfig.runtime.wstring()));runtime->setObjectName("runtimePath");runtime->setPlaceholderText("DDC SDK folder or emacepl.dll — blank uses this PC's configured / detected SDK");runtime->setAccessibleName("DDC SDK folder or runtime DLL");runtime->setToolTip("An explicit path takes priority. Otherwise DDC_SDK_ROOT is used before installed SDK discovery. No SDK version number is fixed.");detailLayout->addWidget(runtime);controls<<runtime;auto evidence=new QLabel;evidence->setWordWrap(true);evidence->setTextInteractionFlags(Qt::TextSelectableByMouse);detailLayout->addWidget(evidence);detailLayout->addWidget(new QLabel("AeroTrace "+QCoreApplication::applicationVersion()+" · "+QCoreApplication::applicationFilePath()));checks->addWidget(details);details->hide();connect(detailsButton,&QPushButton::toggled,details,&QWidget::setVisible);
  auto runtimeActions=new QHBoxLayout;detailLayout->addLayout(runtimeActions);
  auto automatic=new QPushButton("Use automatic selection");automatic->setObjectName("automaticDdcRuntime");desktop::actionIcon(automatic,"restore");runtimeActions->addWidget(automatic);controls<<automatic;
  auto chooseRuntime=new QPushButton("Choose SDK folder…");desktop::actionIcon(chooseRuntime,"folder");runtimeActions->addWidget(chooseRuntime);controls<<chooseRuntime;runtimeActions->addStretch();
  connect(automatic,&QPushButton::clicked,runtime,[this,runtime,evidence]{runtime->clear();backend.captureConfig.connectionPolicy=aero::DdcConnectionPolicy::QualifiedAutomatic;settings.setValue("ddcCompatibility",false);rescanDdc(evidence);});
  connect(chooseRuntime,&QPushButton::clicked,runtime,[this,runtime]{const auto folder=QFileDialog::getExistingDirectory(this,"Select installed DDC SDK",runtime->text().isEmpty()?"C:/DDC":runtime->text());if(!folder.isEmpty())runtime->setText(folder);});
  auto managerRow=new QHBoxLayout;detailLayout->addLayout(managerRow);auto managerPath=new QLineEdit(settings.value("ddcCardManagerPath").toString());managerPath->setObjectName("ddcCardManagerPath");managerPath->setAccessibleName("Card Manager executable override");managerPath->setPlaceholderText("Card Manager executable — blank detects C:/DDC and registered locations");managerRow->addWidget(managerPath,1);controls<<managerPath;
  auto chooseManager=new QPushButton("Choose Card Manager…");desktop::actionIcon(chooseManager,"device");managerRow->addWidget(chooseManager);controls<<chooseManager;
  connect(managerPath,&QLineEdit::textChanged,this,[this](const QString &path){settings.setValue("ddcCardManagerPath",path);});
  connect(chooseManager,&QPushButton::clicked,managerPath,[this,managerPath]{const auto file=QFileDialog::getOpenFileName(this,"Select DDC Card Manager","C:/DDC","Windows application (*.exe)");if(!file.isEmpty())managerPath->setText(file);});
  connect(runtime,&QLineEdit::textChanged,this,[this](const QString &path){backend.captureConfig.runtime=std::filesystem::path(path.toStdWString());settings.setValue("ddcRuntime",path);backend.captureConfig.connectionPolicy=path.trimmed().isEmpty()?aero::DdcConnectionPolicy::QualifiedAutomatic:aero::DdcConnectionPolicy::Compatibility;settings.setValue("ddcCompatibility",!path.trimmed().isEmpty());configurationChanged();});
  simulationOptions->setVisible(!backend.captureConfig.hardware);
  connect(source,&QComboBox::currentIndexChanged,this,[this,source,simulationOptions](int index){
    selectCaptureSource(index);
    const QSignalBlocker block(source);
    source->setCurrentIndex(backend.captureConfig.hardware?0:1);
    simulationOptions->setVisible(!backend.captureConfig.hardware);
  });
  connect(scan,&QPushButton::clicked,this,[this,evidence]{rescanDdc(evidence);});
  auto previous=std::make_shared<QJsonObject>();const auto refresh=[this,table,empty,deviceCount,scanProgress,animate,summary,evidence,arm,scan,manager,controls,previous,mode,helper,checkTexts,checkStatuses,detailsButton]{
    const auto metrics=backend.acquisition.metrics(false);const bool owned=backend.acquisition.connected()||backend.acquisition.active()||connecting||connectAfterDiscovery||savingSession||closingCapture||metrics.cleanupBlocked;for(auto control:controls)control->setEnabled(!owned);manager->setEnabled(!owned);scan->setEnabled(!discoveryRunning);arm->setEnabled(!owned);arm->setText(connecting?"Checking devices…":backend.acquisition.connected()?"Devices connected":backend.captureConfig.hardware?"Connect devices":"Start offline simulation");
    if(*previous!=hardwareDiscovery){*previous=hardwareDiscovery;const auto rows=hardwareDiscovery.value("devices").toArray();table->setRowCount(rows.size());for(int i=0;i<rows.size();++i){const auto o=rows[i].toObject();const QStringList values{o.value("name").toString(),o.value("hardware_ids").toString(),o.value("driver_service").toString()+" / "+o.value("driver_version").toString(),o.value("pnp_status").toString()+" / problem "+QString::number(o.value("pnp_problem").toInt())};for(int col=0;col<4;++col){auto cell=new QTableWidgetItem(values[col]);cell->setToolTip(values[col]);table->setItem(i,col,cell);}}evidence->setText(desktop::discoverySummary(hardwareDiscovery));}
    const auto states=[&](int c){const auto &d=metrics.devices[c];const auto count=metrics.sourceCounts[c*2]+metrics.sourceCounts[c*2+1];switch(d.phase){case aero::DevicePhase::Monitoring:return count?QString("Receiving %1 records").arg(count):QString("Initialized — no traffic yet");case aero::DevicePhase::Simulating:return QString("Simulation — %1 records").arg(count);case aero::DevicePhase::Failed:return "Failed: "+QString::fromStdString(d.message);case aero::DevicePhase::Checking:return QString("Checking logical channel");case aero::DevicePhase::Ready:return QString("Configured");case aero::DevicePhase::Disabled:return QString("Disabled");default:return d.message.empty()?QString("Not checked"):QString::fromStdString(d.message);}};
    QStringList drivers;for(auto item:hardwareDiscovery.value("devices").toArray()){auto d=item.toObject();drivers<<d.value("driver_service").toString()+" "+d.value("driver_version").toString()+" / "+d.value("pnp_status").toString();}
    const auto presence=hardwareDiscovery.value("physical_presence").toString();
    const int count=hardwareDiscovery.value("detected_devices").toInt();
    const auto scanText=discoveryRunning?QString("Scanning…"):hardwareDiscovery.isEmpty()?QString("Not scanned"):
      presence=="unknown"?QString("Unknown — enumeration incomplete"):QString("%1 detected").arg(count);
    if(deviceCount->text()!=scanText)deviceCount->setText(scanText);
    scanProgress->setVisible(animate&&discoveryRunning);
    scan->setText(discoveryRunning?"Scanning…":"Rescan DDC");
    const auto emptyTitle=discoveryRunning?"Scanning for DDC devices":presence=="absent"?"No DDC devices found":
      presence=="unknown"?"Device inventory incomplete":"Windows devices";
    const auto emptyDetail=discoveryRunning?"Checking Windows devices and driver bindings.":
      "Connect a DDC card and click Rescan DDC, or switch Source above to Offline Simulation.";
    empty->showState(table->rowCount()==0,emptyTitle,emptyDetail);

    const auto pack=backend.captureConfig.backend==aero::DdcBackend::HelperX86?helper->currentText():
      hardwareDiscovery.value("provider_id").toString("No matching native provider selected");
    const QStringList values{scanText,drivers.isEmpty()?QString("Not confirmed"):drivers.join("; "),
      desktop::runtimeReadiness(hardwareDiscovery),
      QString("CH1 = %1, CH2 = %2 (vendor mapping required)").arg(backend.captureConfig.logicalDevices[0]).arg(backend.captureConfig.logicalDevices[1]),
      states(0),states(1),hardwareDiscovery.value("runtime_path").toString("Not resolved"),pack,mode->currentText()};
    const auto channelIcon=[&](int index) {
      const auto phase=metrics.devices[index].phase;
      if(phase==aero::DevicePhase::Ready||phase==aero::DevicePhase::Monitoring)return "verified";
      if(phase==aero::DevicePhase::Failed)return "status-error";
      return phase==aero::DevicePhase::Checking?"status-busy":"status-neutral";
    };
    const QStringList statuses{discoveryRunning?"status-busy":presence=="detected"?"verified":"status-neutral",
      "status-neutral","status-neutral","status-neutral",channelIcon(0),channelIcon(1),
      "status-neutral","status-neutral","status-neutral"};
    for(int row=0;row<values.size();++row) {
      if(checkTexts[row]->text()!=values[row])checkTexts[row]->setText(values[row]);
      if(checkStatuses[row]->property("iconName").toString()!=statuses[row]) {
        checkStatuses[row]->setProperty("iconName",statuses[row]);
        desktop::labelIcon(checkStatuses[row],statuses[row],16);
      }
      const bool summaryRow=(row==0 || row==4 || row==5 || (row==8 && backend.captureConfig.hardware));
      const bool show=summaryRow || detailsButton->isChecked();
      for(int column=0;column<4;++column)
        static_cast<QGridLayout *>(summary->layout())->itemAtPosition(row,column)->widget()->setVisible(show);
    }
  };refresh();connect(detailsButton,&QPushButton::toggled,summary,[refresh](bool){refresh();});connect(&tick,&QTimer::timeout,summary,refresh);if(hardwareDiscovery.isEmpty()&&!discoveryRunning)QTimer::singleShot(0,evidence,[this,evidence]{rescanDdc(evidence);});
}

void Window::selectCaptureSource(int mode) {
  if(connecting || savingSession || backend.acquisition.active() || backend.acquisition.connected()) {
    toast("Stop and disconnect before changing the capture source.");
    return;
  }
  backend.captureConfig.hardware=mode==0;
  settings.setValue("captureSource",mode==0?"DDC":"SIMULATOR");
  settings.sync();
  configurationChanged();
  refreshOperatorState();
}

void Window::restoreCaptureSource() {
  const auto mode=settings.value("captureSource","DDC").toString();
  backend.captureConfig.hardware=mode=="DDC";
  if(mode=="EXAMPLE" || mode=="MAPPED_ICD") settings.setValue("captureSource","SIMULATOR");
}
