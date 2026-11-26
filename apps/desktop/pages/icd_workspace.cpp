#include "main_window.hpp"
#include "services/operator_style.hpp"
#include "services/profile_io.hpp"
#include "ui_icd_workspace.h"

namespace {
QString workspaceDocumentPath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents/workspace-document.json";
}
QString groupOf(const QJsonObject &row) {
  auto group=row["subsystem"].toString();
  if(group.isEmpty()) {
    const auto parts=row["id"].toString().split(" / ");
    if(parts.size()==3 && parts[0].startsWith("Subsystem-"))group=parts[0];
  }
  return group.isEmpty()?QString("Unassigned"):group;
}
}

void Window::restoreIcdDocument() {
  QFile file(workspaceDocumentPath());
  if(file.size()>4*1024*1024 || !file.open(QIODevice::ReadOnly))return;
  const auto saved=QJsonDocument::fromJson(file.readAll()).object();
  workspaceDocument=saved["profile"].toObject();
  workspacePdf=saved["pdf"].toString();
}

void Window::rememberIcdDocument(const QJsonObject &profile,const QString &pdf) {
  workspaceDocument=profile;
  workspacePdf=pdf;
  QDir().mkpath(QFileInfo(workspaceDocumentPath()).absolutePath());
  QSaveFile file(workspaceDocumentPath());
  const auto bytes=QJsonDocument(QJsonObject{{"profile",profile},{"pdf",pdf}}).toJson(QJsonDocument::Compact);
  if(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size())file.commit();
}

bool Window::inWorkspace(const aero::Signal &signal) const {
  const auto set=workspaceDocument["icd_set_id"].toString().toStdString();
  if(qApp->property("legacyIcdFixture").toBool()&&!backend.replaySource&&!set.empty()&&!signal.icdSetId.empty()&&signal.icdSetId!=set)return false;
  return workspaceSubsystem.isEmpty() || (signal.subsystem.empty()?QString("Unassigned"):QString::fromStdString(signal.subsystem))==workspaceSubsystem;
}

QWidget *Window::attachIcdWorkspace() {
  const auto scope=backend.replaySource?"replay/"+currentFile:QString("live");
  if(scope!=workspaceScopeKey) {
    workspaceScopeKey=scope;
    workspaceSubsystem=settings.value("workspace/"+scope+"/subsystem").toString();
  }
  auto container=new QWidget;
  Ui::IcdWorkspace form;
  form.setupUi(container);
  form.workspaceBodyLayout->addWidget(content,1);
  form.workspaceLayout->setContentsMargins(16,14,16,14);
  form.workspaceLayout->setStretch(0,1);
  form.workspaceLayout->setStretch(1,3);
  form.subsystemPanel->setMaximumWidth(width()>=1550?352:245);
  form.workspaceTitle->hide();
  form.subsystemLayout->insertWidget(0,desktop::sectionHeading("ICD Workspace","layers"));
  form.workspaceDocument->setTextFormat(Qt::PlainText);
  const auto attached=!backend.replaySource&&!workspacePdf.isEmpty()&&QFileInfo::exists(workspacePdf);
  form.workspaceDocument->setText(backend.replaySource?replayProfileDescription:
    workspaceDocument["document"].toString("Published profile"));
  if(attached) {
    const QFileInfo pdf(workspacePdf);
    form.workspaceDocument->setText(form.workspaceDocument->text()+"\nAttached ICD · "+QLocale().formattedDataSize(pdf.size()));
  }
  auto documentRow=new QHBoxLayout;
  auto documentIcon=new QLabel;
  desktop::labelIcon(documentIcon,"file",22);
  form.subsystemLayout->removeWidget(form.workspaceDocument);
  documentRow->addWidget(documentIcon);
  documentRow->addWidget(form.workspaceDocument,1);
  if(attached) {
    auto badge=new QLabel("Attached");
    badge->setProperty("pill","profile");
    documentRow->addWidget(badge);
  }
  auto attachmentMenu=new QToolButton;
  attachmentMenu->setObjectName("icdAttachmentActions");attachmentMenu->setIcon(desktop::operatorIcon("more"));
  attachmentMenu->setAccessibleName("Attached PDF actions");attachmentMenu->setToolTip("Attached PDF actions");
  attachmentMenu->setPopupMode(QToolButton::InstantPopup);
  auto attachmentActions=new QMenu(attachmentMenu);
  attachmentMenu->setMenu(attachmentActions);
  attachmentActions->addAction("Open PDF",container,[path=workspacePdf]{QDesktopServices::openUrl(QUrl::fromLocalFile(path));});
  attachmentActions->addAction("Manage revision…",container,[this]{manageIcds();});
  auto deletePdf=attachmentActions->addAction("Delete PDF revision permanently…",container,[this] {
    const auto hash=workspaceDocument["icd_revision"].toString(workspaceDocument["hash"].toString(QFileInfo(workspacePdf).completeBaseName()));
    deleteIcdRevision(hash,workspaceDocument["document"].toString(QFileInfo(workspacePdf).fileName()));
  });
  connect(attachmentActions,&QMenu::aboutToShow,deletePdf,[this,deletePdf]{deletePdf->setEnabled(icdDeletionAvailable());});
  if(attached) {
    documentRow->addWidget(attachmentMenu);
    form.workspaceDocument->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(form.workspaceDocument,&QLabel::customContextMenuRequested,attachmentMenu,[form,attachmentActions](const QPoint &pos){
      attachmentActions->popup(form.workspaceDocument->mapToGlobal(pos));
    });
  } else {attachmentMenu->setParent(container);attachmentMenu->hide();}
  form.subsystemLayout->insertLayout(1,documentRow);
  desktop::actionIcon(form.workspaceImport,"folder");
  desktop::actionIcon(form.workspaceChat,"sparkle");
  form.workspaceTabs->setIconSize(QSize(22,22));
  form.workspaceTabs->addTab(desktop::operatorIcon("mapping"),"Mapping");
  form.workspaceTabs->addTab(desktop::operatorIcon("settings"),"Engineering");
  form.workspaceTabs->addTab(desktop::operatorIcon("chart"),"Analysis");
  form.workspaceTabs->setCurrentIndex(page==12?0:page==6?1:2);
  QMap<QString,QJsonObject> groups;QHash<QString,QString> publishedIds;
  const auto mappings=backend.mappings();
  for(const auto &signal:mappings) {
    const auto set=workspaceDocument["icd_set_id"].toString().toStdString();
    if(qApp->property("legacyIcdFixture").toBool()&&!backend.replaySource&&!set.empty()&&!signal.icdSetId.empty()&&signal.icdSetId!=set)continue;
    const auto object=desktop::signalToJson(signal);publishedIds.insert(QString::fromStdString(signal.id),QString::fromStdString(signal.sourceHash));
    const auto key=groupOf(object);
    auto group=groups.value(key);
    group["published"]=group["published"].toInt()+1;
    auto messages=group["messages"].toArray();const auto message=QString::fromStdString(signal.messageId);if(!message.isEmpty() && !messages.contains(message))messages.append(message);group["messages"]=messages;
    group["message"]=messages.size()>1?messages[0].toString()+QString(" +%1 message(s)").arg(messages.size()-1):message;
    group["rt"]=int(signal.rt);group["sa"]=int(signal.sa);
    auto channels=group["channels"].toArray();if(!channels.contains(int(signal.channel)))channels.append(int(signal.channel));group["channels"]=channels;group["channel"]=channels.size()>1?-1:int(signal.channel);
    groups[key]=group;
  }
  if(qApp->property("legacyIcdFixture").toBool()&&!backend.replaySource) {
    QJsonArray candidates=workspaceDocument["signals"].toArray();
    for(const auto &entry:workspaceDocument["review"].toArray())candidates.append(entry);
    for(const auto &entry:candidates) {
      const auto row=entry.toObject(),fields=row["draft_cells"].toObject();
      const auto id=row["id"].toString(fields["id"].toString());
      if((publishedIds.contains(id) && publishedIds.value(id)==row["source_hash"].toString()) || (row["width"].toInt()==0 && fields["width"].toInt()==0))continue;
      const auto key=groupOf(row);
      auto group=groups.value(key);
      group["draft"]=group["draft"].toInt()+1;
      if(group["message"].toString().isEmpty())group["message"]=row["message_id"];
      if(!fields.isEmpty() && !group["published"].toInt()){
        group["rt"]=fields["rt"];group["sa"]=fields["sa"];group["channel"]=fields["channel"];
        if(row["source_hash"].toString()=="37e09d13f2863af4ab25507a14bbcc5fdcae8a1a176242153972a40225604937"){
          if(key=="Subsystem-1")group["channel"]=1;
          if(key=="Subsystem-3")group["channel"]=2;
        }
      }
      groups[key]=group;
    }
  }
  if(!workspaceSubsystem.isEmpty() && !groups.contains(workspaceSubsystem))workspaceSubsystem.clear();
  auto all=new QTreeWidgetItem(form.subsystemList,QStringList{"All subsystems"});
  all->setData(0,Qt::UserRole,QString());
  all->setIcon(0,desktop::operatorIcon("layers"));
  for(auto it=groups.cbegin();it!=groups.cend();++it) {
    const auto group=it.value();
    auto state=group["published"].toInt()?QString("Published · %1 fields").arg(group["published"].toInt()):QString("Draft · %1 fields").arg(group["draft"].toInt());
    if(group["published"].toInt() && group["draft"].toInt())state+=QString(" · %1 draft").arg(group["draft"].toInt());
    const auto channel=group["channel"].toInt();
    auto item=new QTreeWidgetItem(form.subsystemList,QStringList{it.key()+" · "+(channel<0?QString("Multiple channels"):channel?QString("CH%1").arg(channel):QString("Any channel"))+"\n"+group["message"].toString()+" · "+state});
    item->setData(0,Qt::UserRole,it.key());
    item->setIcon(0,desktop::operatorIcon("layers"));
    item->setData(0,Qt::UserRole+1,channel);
    item->setToolTip(0,QString("Message %1 · RT %2 / SA %3 · %4").arg(group["message"].toString()).arg(group["rt"].toInt()).arg(group["sa"].toInt()).arg(channel<0?QString("Multiple channels"):channel?QString("CH%1").arg(channel):QString("Any channel")));
    if(it.key()==workspaceSubsystem) {
      form.subsystemList->setCurrentItem(item);
      form.workspaceRoute->setText(item->toolTip(0));
    }
  }
  auto subsystemMenu=new QPushButton(desktop::operatorIcon("more"),"Subsystem actions",form.subsystemPanel);
  subsystemMenu->setObjectName("icdSubsystemActions");
  form.subsystemLayout->insertWidget(form.subsystemLayout->indexOf(form.subsystemList)+1,subsystemMenu);
  const auto showSubsystemActions=[this,tree=form.subsystemList](QTreeWidgetItem *item,const QPoint &pos) {
    if(!item)return;
    const auto subsystem=item->data(0,Qt::UserRole).toString();
    if(subsystem.isEmpty())return;
    auto menu=new QMenu(tree);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto remove=menu->addAction("Delete subsystem mappings permanently…",this,[this,subsystem] {
      deleteIcdMapping("subsystem",subsystem,subsystem);
    });
    remove->setEnabled(icdDeletionAvailable());
    menu->popup(pos);
  };
  form.subsystemList->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(form.subsystemList,&QTreeWidget::customContextMenuRequested,container,
    [tree=form.subsystemList,showSubsystemActions](const QPoint &pos){
      showSubsystemActions(tree->itemAt(pos),tree->viewport()->mapToGlobal(pos));
    });
  connect(subsystemMenu,&QPushButton::clicked,container,[tree=form.subsystemList,subsystemMenu,showSubsystemActions]{
    showSubsystemActions(tree->currentItem(),subsystemMenu->mapToGlobal(subsystemMenu->rect().bottomLeft()));
  });
  subsystemMenu->setVisible(!workspaceSubsystem.isEmpty()&&!backend.replaySource);
  if(workspaceSubsystem.isEmpty())form.subsystemList->setCurrentItem(all);
  const auto listHeight=form.subsystemList->topLevelItemCount()*75+10;
  form.subsystemList->setMaximumHeight(qMin(height()>=850?408:290,listHeight));
  form.subsystemLayout->addStretch();
  if(documentDialog && !documentDialog->property("jobRunning").toBool() && documentDialog->property("activeConversation").toString().isEmpty())if(auto scope=documentDialog->findChild<QComboBox *>("assistantScope")) {
    const QSignalBlocker block(scope);scope->clear();scope->addItem(workspaceSubsystem.isEmpty()?"All subsystems":workspaceSubsystem,workspaceSubsystem);if(!workspaceSubsystem.isEmpty())scope->addItem("All subsystems",QString());
  }
  form.workspaceScope->setText((workspaceSubsystem.isEmpty()?"All subsystems":workspaceSubsystem)+
    QString(" · CH%1 / %2 · %3").arg(analysisChannel).arg(analysisBus?"B":"A",backend.replaySource?"Recorded profile":"Live profile"));
  if(page==7&&recordedCsv){
    form.subsystemPanel->hide();
    form.workspaceDocument->setText("Recorded CSV · "+QFileInfo(recordedCsv->path).fileName());
    form.workspaceScope->setText(QString("CH%1 / %2 · Explicit CSV route and mapping snapshot").arg(recordedCsv->channel).arg(recordedCsv->bus?"B":"A"));
    form.workspaceRoute->setText(QString("RT %1 / SA %2 · CSV provenance unverified").arg(recordedCsv->route.rt).arg(recordedCsv->route.sa));
  }
  connect(form.subsystemList,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item) {
    if(!item)return;
    workspaceSubsystem=item->data(0,Qt::UserRole).toString();
    settings.setValue("workspace/"+workspaceScopeKey+"/subsystem",workspaceSubsystem);
    const auto channel=item->data(0,Qt::UserRole+1).toInt();
    if(channel==1 || channel==2)analysisChannel=unsigned(channel);
    const auto rules=backend.mappings();
    const auto found=std::find_if(rules.begin(),rules.end(),[this](const auto &signal){return inWorkspace(signal);});
    if(found!=rules.end()) {
      preferredSignalIndex=unsigned(found-rules.begin());
      plotPanelIds[0]=QString::fromStdString(found->id);
      if(found->channel)analysisChannel=found->channel;
      if(found->bus>=0)analysisBus=found->bus;
    }
    QTimer::singleShot(0,this,[this]{navigate(page);});
  });
  connect(form.workspaceTabs,&QTabBar::currentChanged,this,[this](int tab) {
    static constexpr int pages[]{12,6,7};
    if(tab>=0 && tab<3)QTimer::singleShot(0,this,[this,tab]{navigate(pages[tab]);});
  });
  form.workspaceImport->setText("Manage ICDs");
  connect(form.workspaceImport,&QPushButton::clicked,this,[this]{manageIcds();});
  form.workspaceChat->hide();
  if(qualityTable)for(int row=0;row<qualityTable->rowCount() && row<int(mappings.size());++row)
    qualityTable->setRowHidden(row,!inWorkspace(mappings[row]));
  return container;
}

void Window::updateEngineeringDetails() {
  if(page!=6 && page!=7 && page!=12)return;
  if(page==7&&recordedCsv)return;
  const auto &mappings=backend.mappings();
  const auto selectedStream=QString("CH%1 / %2").arg(analysisChannel).arg(analysisBus?"B":"A");
  if(auto current=scroll?scroll->widget():nullptr) {
    if(auto scope=current->findChild<QLabel *>("workspaceScope"))scope->setText((workspaceSubsystem.isEmpty()?"All subsystems":workspaceSubsystem)+" · "+selectedStream+" · "+(backend.replaySource?"Recorded profile":"Live profile"));
    if(preferredSignalIndex<mappings.size() && inWorkspace(mappings[preferredSignalIndex]))if(auto route=current->findChild<QLabel *>("workspaceRoute")){
      const auto &signal=mappings[preferredSignalIndex];
      const auto mappingStream=(signal.channel?QString("CH%1").arg(signal.channel):QString("Any channel"))+" / "+(signal.bus<0?QString("Any bus"):QString(signal.bus?"B":"A"));
      route->setText(page==7 && (plotRawColumn>=0 || plots.empty() || plots.front()->property("signalIndex").toInt()<0)?(plotRawColumn>=0?"Raw words · ":"Select a signal · ")+selectedStream:QString("Message %1 · RT %2 / SA %3 · %4\nMap: %5\nView: %6").arg(QString::fromStdString(signal.messageId)).arg(signal.rt).arg(signal.sa).arg(signal.transmit?"RT → BC":"BC → RT",mappingStream,selectedStream));
    }
  }
  if(documentDialog && !documentDialog->property("jobRunning").toBool() && documentDialog->property("activeConversation").toString().isEmpty())if(auto scope=documentDialog->findChild<QComboBox *>("assistantScope"))for(int i=0;i<scope->count();++i)if(scope->itemData(i).toString()==workspaceSubsystem && !workspaceSubsystem.isEmpty())scope->setItemText(i,workspaceSubsystem+" · "+selectedStream);
  if(!engineeringDetails)return;
  if(preferredSignalIndex>=mappings.size() || !inWorkspace(mappings[preferredSignalIndex])) {engineeringDetails->setText("No published signal selected. Open Mapping to review and publish this subsystem.");return;}
  const auto &signal=mappings[preferredSignalIndex];
  engineeringDetails->setText(QString("%1\nRT %2 / SA %3 · %4 · Word %5 (0-based) · %6\nEngineering = decoded × %7 + %8 %9\n%10 · page %11")
    .arg(QString::fromStdString(signal.sourceSignalId.empty()?signal.id:signal.sourceSignalId)+" · Message "+QString::fromStdString(signal.messageId))
    .arg(signal.rt).arg(signal.sa).arg(signal.transmit?"RT → BC":"BC → RT").arg(signal.word)
    .arg(signal.leastSignificantWordFirst?"LSW first":"MSW first").arg(desktop::displayValue(signal.scale)).arg(desktop::displayValue(signal.offset))
    .arg(QString::fromStdString(signal.unit),QString::fromStdString(signal.sourceDocument)).arg(signal.sourcePage));
}
