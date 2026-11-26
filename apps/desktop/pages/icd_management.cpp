#include "main_window.hpp"
#include "services/document_job.hpp"
#include "services/profile_io.hpp"
#include "services/manual_mapping.hpp"
#include "services/runtime_paths.hpp"

void Window::manageIcds() {
  auto dialog=new QDialog(this);dialog->setObjectName("icdManager");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Manage ICDs");dialog->setWindowModality(Qt::WindowModal);dialog->resize(800,520);
  auto layout=new QVBoxLayout(dialog);auto archived=new QCheckBox("Show archived revisions",dialog);layout->addWidget(archived);
  auto list=new QTreeWidget(dialog);list->setHeaderLabels({"ICD","Pages","State / revision"});list->setRootIsDecorated(false);list->setColumnWidth(0,360);layout->addWidget(list,1);
  auto note=new QLabel("Archived ICDs retain evidence and conversations. Permanent deletion removes eligible local copies and retains recording snapshots.");note->setWordWrap(true);layout->addWidget(note);
  auto actions=new QHBoxLayout;layout->addLayout(actions);
  auto add=new QPushButton("Import"),review=new QPushButton("Open PDF"),replace=new QPushButton("Replace revision"),rename=new QPushButton("Rename"),archive=new QPushButton("Archive"),close=new QPushButton("Close");
  auto remove=new QPushButton("Delete permanently…");
  remove->setObjectName("deleteIcdRevision");
  for(auto b:{add,review,replace,rename,archive,remove,close})actions->addWidget(b);
  connect(remove,&QPushButton::clicked,dialog,[this,list,dialog]{
    if(!list->currentItem())return;
    const auto item=list->currentItem()->data(0,Qt::UserRole).toJsonObject();
    deleteIcdRevision(item["hash"].toString(),item["name"].toString());
  });
  auto data=std::make_shared<QJsonObject>();auto profile=std::make_shared<QJsonObject>();auto pdf=std::make_shared<QString>();
  auto refresh=std::make_shared<std::function<void()>>();
  *refresh=[dialog,list,note,archived,data,profile,pdf,review,replace,rename,archive]{
    review->setEnabled(false);replace->setEnabled(false);archive->setEnabled(false);rename->setEnabled(false);
    desktop::libraryJob(dialog,{{"op","library"},{"include_archived",true}},[dialog,list,note,archived,data](QJsonObject result,QString error){if(!error.isEmpty()){note->setText(error);return;}*data=result;const QSignalBlocker block(list);list->clear();for(const auto &v:result["documents"].toArray()){auto doc=v.toObject();if(doc["archived"].toBool()&&!archived->isChecked())continue;auto item=new QTreeWidgetItem(list,QStringList{doc["name"].toString(),QString::number(doc["pages"].toInt()),(doc["archived"].toBool()?"Archived · ":"Available · ")+doc["hash"].toString().left(8)});item->setData(0,Qt::UserRole,doc);}note->setText("Select an ICD to review, replace, rename, or archive. Previous recordings retain their own mappings.");});
  };
  connect(list,&QTreeWidget::currentItemChanged,dialog,[dialog,list,note,profile,pdf,review,replace,rename,archive](QTreeWidgetItem *item){if(!item)return;const auto doc=item->data(0,Qt::UserRole).toJsonObject();const auto hash=doc["hash"].toString();review->setEnabled(false);archive->setEnabled(false);desktop::libraryJob(dialog,{{"op","library"},{"document_hash",hash},{"include_archived",true}},[list,note,profile,pdf,review,replace,rename,archive,hash,doc](QJsonObject result,QString error){if(!list->currentItem()||list->currentItem()->data(0,Qt::UserRole).toJsonObject()["hash"].toString()!=hash)return;if(!error.isEmpty()){note->setText(error);return;}*profile=result["profile"].toObject();*pdf=result["pdf_path"].toString();archive->setText(doc["archived"].toBool()?"Restore":"Archive");review->setEnabled(!doc["archived"].toBool());replace->setEnabled(!doc["archived"].toBool());rename->setEnabled(true);archive->setEnabled(true);note->setText("Revision "+hash.left(12)+" · "+QString::number((*profile)["signals"].toArray().size())+" extracted mappings. Publication remains explicit.");});});
  connect(archived,&QCheckBox::toggled,dialog,[refresh]{(*refresh)();});
  connect(close,&QPushButton::clicked,dialog,&QDialog::close);
  connect(add,&QPushButton::clicked,dialog,[this,dialog]{dialog->close();QTimer::singleShot(0,this,[this]{importIcd();});});
  connect(review,&QPushButton::clicked,dialog,[pdf,note]{
    if(!QFileInfo(*pdf).isFile() || !QDesktopServices::openUrl(QUrl::fromLocalFile(*pdf)))
      note->setText("The source PDF is unavailable.");
  });
  connect(replace,&QPushButton::clicked,dialog,[this,dialog,list]{if(!list->currentItem())return;const auto doc=list->currentItem()->data(0,Qt::UserRole).toJsonObject();const auto file=QFileDialog::getOpenFileName(dialog,"Import a replacement ICD profile",{},"JSON or CSV (*.json *.csv)");if(file.isEmpty())return;setProperty("icdReplacementSet",doc["icd_set_id"].toString());setProperty("icdReplacementRevision",doc["hash"].toString());dialog->close();importIcdPath(file);});
  connect(rename,&QPushButton::clicked,dialog,[dialog,list,refresh,note]{if(!list->currentItem())return;const auto doc=list->currentItem()->data(0,Qt::UserRole).toJsonObject();bool ok=false;const auto name=QInputDialog::getText(dialog,"Rename ICD","Library name",QLineEdit::Normal,doc["name"].toString(),&ok);if(!ok)return;desktop::libraryJob(dialog,{{"op","library_rename"},{"document_hash",doc["hash"]},{"name",name}},[note,refresh](QJsonObject,QString error){if(error.isEmpty())(*refresh)();else note->setText(error);});});
  connect(archive,&QPushButton::clicked,dialog,[this,dialog,list,refresh,note,archive]{
    if(property("icdDeletePending").toBool() || property("icdArchivePending").toBool()){note->setText("Wait for the current library change to finish.");return;}
    if(!list->currentItem()||backend.replaySource){note->setText("Return to the live workspace to change library activation.");return;}
    const auto doc=list->currentItem()->data(0,Qt::UserRole).toJsonObject();
    const auto hash=doc["hash"].toString();const bool restoring=doc["archived"].toBool();
    archive->setEnabled(false);
    setProperty("icdArchivePending",true);
    desktop::libraryJob(this,{{"op",restoring?"library_restore":"library_archive"},{"document_hash",hash}},[this,guard=QPointer<QDialog>(dialog),note,refresh,restoring,hash](QJsonObject,QString error){
      setProperty("icdArchivePending",false);
      if(!error.isEmpty()){if(guard)note->setText(error);else toast(error);return;}
      if(!restoring&&(workspaceDocument["hash"].toString()==hash || (workspaceDocument["icd_revision"].toString().isEmpty()&&QFileInfo(workspacePdf).completeBaseName()==hash))){rememberIcdDocument({},{});workspaceSubsystem.clear();}
      if(!restoring&&workspaceDocument["icd_revision"].toString()==hash){rememberIcdDocument({},{});workspaceSubsystem.clear();}
      if(guard)(*refresh)();toast(restoring?"PDF restored to the Assistant library.":"PDF archived. Evidence and conversations were retained.");
    });
  });
  if(property("icdDeletePending").toBool()) {
    auto retry=new QPushButton("Retry pending deletion",dialog);
    actions->insertWidget(0,retry);
    connect(retry,&QPushButton::clicked,this,&Window::resumeIcdDeletion);
  }
  (*refresh)();dialog->show();
}


namespace {
QString deletionPath() {
  return desktop::dataDirectory()+"/pending-icd-deletion.json";
}

bool writeDeletion(const QJsonObject &record) {
  const auto path=deletionPath();
  if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
  QSaveFile file(path);
  const auto data=QJsonDocument(record).toJson(QJsonDocument::Compact);
  return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();
}

struct DeletionScope {
  QJsonObject data;
  QString kind=data["kind"].toString();
  QString identity=data["identity"].toString();
  QString owner=data["set"].toString();
  QString revision=data["revision"].toString();
  bool document=kind=="pdf";

  bool rule(const aero::Signal &signal) const {
    const auto eq=[](const std::string &value,const QString &text){return QString::fromStdString(value)==text;};
    if(document) {
      if(eq(signal.icdRevision,identity))return true;
      return signal.icdRevision.empty() && (!signal.sourceHash.empty() &&
        (eq(signal.sourceHash,identity)||data["purge_hashes"].toArray().contains(QString::fromStdString(signal.sourceHash))));
    }
    if(!owner.isEmpty()&&!eq(signal.icdSetId,owner))return false;
    if(!revision.isEmpty()&&!eq(signal.icdRevision,revision))return false;
    if(kind=="field")return eq(signal.id,identity);
    if(kind=="command")return eq(signal.messageId,identity);
    return signal.subsystem.empty()?identity=="Unassigned":eq(signal.subsystem,identity);
  }

  QJsonArray drafts(const QJsonArray &previous) const {
    QJsonArray result;
    for(const auto &value:previous) {
      auto command=value.toObject();
      const auto id=command["id"].toString();
      const auto fields=command["fields"].toArray();
      if(document && command["source_revision"]==identity)continue;
      if(document || (!owner.isEmpty()&&owner!="manual")){result.append(command);continue;}
      if(kind=="subsystem" && command["subsystem"]==identity)continue;
      if(kind=="command" && id==identity)continue;
      if(kind=="field") {
        QJsonArray kept;
        for(const auto &field:fields)
          if("manual/"+id+"/"+field.toObject()["id"].toString()!=identity)kept.append(field);
        if(kept.size()!=fields.size()) {
          if(kept.isEmpty())continue;
          command["fields"]=kept;
        }
      }
      result.append(command);
    }
    return result;
  }
};
}

bool Window::icdDeletionAvailable() const {
  return unlocked&&!backend.replaySource&&!backend.acquisition.active()&&!savingSession&&
    !connecting&&!closingCapture&&!property("icdArchivePending").toBool()&&
    (!documentDialog || (!documentDialog->property("jobRunning").toBool()&&
                         documentDialog->property("queuedIcdJobs").toInt()==0))&&
    !findChild<QDialog *>("manualMappingDialog");
}

void Window::deleteIcdRevision(const QString &hash,const QString &label) {
  if(property("icdDeletePending").toBool()){resumeIcdDeletion();return;}
  if(!icdDeletionAvailable()){toast("Finish capture, save, editing and Assistant jobs before deleting an ICD.");return;}
  if(hash.isEmpty()){manageIcds();return;}
  setProperty("icdArchivePending",true);
  desktop::libraryJob(this,{{"op","library_delete_preview"},{"document_hash",hash}},
    [this,hash,label](QJsonObject result,QString error) {
      setProperty("icdArchivePending",false);
      if(!error.isEmpty()){toast(error);return;}
      stageIcdDeletion({{"kind","pdf"},{"identity",hash},{"purge_hashes",result["purge_hashes"]}},label);
    });
}

void Window::deleteIcdMapping(const QString &kind,const QString &identity,const QString &label,
                              const QString &set,const QString &revision) {
  if(identity.isEmpty())return;
  if(property("icdDeletePending").toBool()){resumeIcdDeletion();return;}
  stageIcdDeletion({{"kind",kind},{"identity",identity},{"set",set},{"revision",revision}},label);
}

void Window::stageIcdDeletion(const QJsonObject &scope,const QString &label) {
  if(!icdDeletionAvailable()){toast("Finish capture, save, editing and Assistant jobs before deleting mappings.");return;}
  if(QFileInfo::exists(deletionPath())){resumeIcdDeletion();return;}
  const DeletionScope selected{scope};
  const auto previous=desktop::profileToJson(backend.profile);
  const auto previousManual=manualRegistry;
  const auto previousDrafts=desktop::readManualDrafts();
  std::vector<aero::Signal> kept;
  for(const auto &signal:backend.profile)if(!selected.rule(signal))kept.push_back(signal);
  auto manual=manualRegistry;
  auto drafts=previousDrafts;
  if(manual.contains("commands"))manual["commands"]=selected.drafts(manual["commands"].toArray());
  if(drafts.contains("commands"))drafts["commands"]=selected.drafts(drafts["commands"].toArray());
  const auto removed=backend.profile.size()-kept.size();
  if(!selected.document&&!removed&&manual==previousManual&&drafts==previousDrafts) {
    toast("The selected mapping is already absent.");return;
  }
  if(auto error=aero::validateChecks(backend.captureConfig.checkRules,kept);!error.empty()){
    toast("Update the dependent signal checks before deleting: "+QString::fromStdString(error));return;
  }
  const QString impact=selected.document?
    QString("ICD revision: %1\nLocal PDF copies: %2\nActive fields: %3").arg(selected.identity.left(12))
      .arg(scope["purge_hashes"].toArray().size()).arg(removed):
    QString("Mapping: %1\nActive fields: %2").arg(selected.kind).arg(removed);
  if(QMessageBox::question(this,"Permanently delete?",QString("%1\n\n%2\nMatching drafts are removed. Saved recordings and chat history are retained.").arg(label,impact),
       QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
  bool confirmed=false;
  if(QInputDialog::getText(this,"Confirm permanent deletion","Type DELETE to confirm.",
      QLineEdit::Normal,{},&confirmed)!="DELETE"||!confirmed)return;
  if(!icdDeletionAvailable()||desktop::profileToJson(backend.profile)!=previous||
     manualRegistry!=previousManual||desktop::readManualDrafts()!=previousDrafts) {
    toast("The workspace changed. Review the deletion scope again.");return;
  }
  auto record=scope;
  record["profile"]=desktop::profileToJson(kept);
  record["manual"]=manual;
  record["drafts"]=drafts;
  if(!writeDeletion(record)){toast("Could not journal ICD deletion.");return;}
  resumeIcdDeletion();
}

void Window::resumeIcdDeletion() {
  if(property("icdDeletionRunning").toBool())return;
  QFile file(deletionPath());
  if(!file.exists())return;
  setProperty("icdDeletePending",true);
  if(backend.acquisition.active()||savingSession||connecting)return;
  if(!file.open(QIODevice::ReadOnly)||file.size()>64*1024*1024){toast("Cannot read pending ICD deletion.");return;}
  const auto record=QJsonDocument::fromJson(file.readAll()).object();file.close();
  std::vector<aero::Signal> signalsAfter;
  const auto error=desktop::profileFromJson(record["profile"].toObject(),signalsAfter);
  if(!error.isEmpty()){toast("Pending ICD deletion could not load its profile: "+error);return;}
  const auto registry=record["manual"].toObject();
  icdDeletionApplying=true;
  const auto published=publishProfile(signalsAfter,&registry);
  icdDeletionApplying=false;
  if(!published.isEmpty()||!desktop::saveManualDrafts(record["drafts"].toObject())) {
    toast(published.isEmpty()?"Could not save draft changes. Retry pending deletion in Manage ICDs.":published);
    return;
  }
  const auto finish=[this](bool clean) {
    setProperty("icdDeletionRunning",false);
    if(clean && QFile::remove(deletionPath())) {
      setProperty("icdDeletePending",false);
      if(auto dialog=findChild<QDialog *>("icdManager"))dialog->close();
      if(page==12||page==6||page==7)navigate(page);
      toast("Deletion complete.");
    } else toast("Deletion is pending. Close the local PDF viewer, then retry in Manage ICDs.");
  };
  if(record["kind"]!="pdf"){finish(true);return;}
  const auto hash=record["identity"].toString();
  if(workspaceDocument["icd_revision"]==hash||workspaceDocument["hash"]==hash||
      QFileInfo(workspacePdf).completeBaseName()==hash||
      record["purge_hashes"].toArray().contains(QFileInfo(workspacePdf).completeBaseName())) {
    rememberIcdDocument({},{});workspaceSubsystem.clear();
  }
  if(settings.value("icdChatDocument").toString()==hash)settings.remove("icdChatDocument");
  const QDir proposalDir(desktop::dataDirectory()+"/Documents/mapping-drafts");
  const auto proposals=proposalDir.entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks);
  for(const auto &item:proposals) {
    QFile draft(item.absoluteFilePath());
    if(!draft.open(QIODevice::ReadOnly)||draft.size()>1024*1024)continue;
    const auto proposal=QJsonDocument::fromJson(draft.readAll()).object();draft.close();
    if(proposal["document_hash"]==hash&&!proposal["published"].toBool()&&!draft.remove()) {
      toast("Could not remove an ICD draft. Retry pending deletion in Manage ICDs.");return;
    }
  }
  closeDocumentCenter();
  setProperty("icdDeletionRunning",true);
  desktop::libraryJob(this,{{"op","library_delete"},{"document_hash",hash},{"purge_hashes",record["purge_hashes"]}},
    [finish,this](QJsonObject result,QString failure) {
      finish(failure.isEmpty()&&!result["cleanup_pending"].toBool());
      if(!failure.isEmpty())toast(failure);
    });
}
