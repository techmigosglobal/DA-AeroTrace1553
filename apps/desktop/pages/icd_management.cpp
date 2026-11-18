#include "main_window.hpp"
#include "services/document_job.hpp"
#include "services/profile_io.hpp"

void Window::manageIcds() {
  auto dialog=new QDialog(this);dialog->setObjectName("icdManager");dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Manage ICDs");dialog->setWindowModality(Qt::WindowModal);dialog->resize(800,520);
  auto layout=new QVBoxLayout(dialog);auto archived=new QCheckBox("Show archived revisions",dialog);layout->addWidget(archived);
  auto list=new QTreeWidget(dialog);list->setHeaderLabels({"ICD","Pages","State / revision"});list->setRootIsDecorated(false);list->setColumnWidth(0,360);layout->addWidget(list,1);
  auto note=new QLabel("Archived ICDs retain evidence and conversations. Supplier PDFs and recording snapshots are never deleted.");note->setWordWrap(true);layout->addWidget(note);
  auto actions=new QHBoxLayout;layout->addLayout(actions);
  auto add=new QPushButton("Import"),review=new QPushButton("Open / edit"),replace=new QPushButton("Replace revision"),rename=new QPushButton("Rename"),archive=new QPushButton("Archive"),close=new QPushButton("Close");
  for(auto b:{add,review,replace,rename,archive,close})actions->addWidget(b);
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
  connect(review,&QPushButton::clicked,dialog,[this,dialog,profile,pdf]{auto copy=*profile;auto source=*pdf;rememberIcdDocument(copy,source);dialog->close();navigate(12);reviewDocument(copy,source);});
  connect(replace,&QPushButton::clicked,dialog,[this,dialog,list]{if(!list->currentItem())return;const auto doc=list->currentItem()->data(0,Qt::UserRole).toJsonObject();const auto file=QFileDialog::getOpenFileName(dialog,"Import a replacement ICD revision",{},"PDF (*.pdf)");if(file.isEmpty())return;setProperty("icdReplacementSet",doc["icd_set_id"].toString());setProperty("icdReplacementRevision",doc["hash"].toString());dialog->close();documentCenter(file);});
  connect(rename,&QPushButton::clicked,dialog,[dialog,list,refresh,note]{if(!list->currentItem())return;const auto doc=list->currentItem()->data(0,Qt::UserRole).toJsonObject();bool ok=false;const auto name=QInputDialog::getText(dialog,"Rename ICD","Library name",QLineEdit::Normal,doc["name"].toString(),&ok);if(!ok)return;desktop::libraryJob(dialog,{{"op","library_rename"},{"document_hash",doc["hash"]},{"name",name}},[note,refresh](QJsonObject,QString error){if(error.isEmpty())(*refresh)();else note->setText(error);});});
  connect(archive,&QPushButton::clicked,dialog,[this,dialog,list,data,profile,refresh,note,archive]{
    if(property("icdArchivePending").toBool()){note->setText("Wait for the current library change to finish.");return;}
    if(!list->currentItem()||backend.replaySource){note->setText("Return to the live workspace to change library activation.");return;}
    const auto doc=list->currentItem()->data(0,Qt::UserRole).toJsonObject();const auto hash=doc["hash"].toString(),set=doc["icd_set_id"].toString();const bool restoring=doc["archived"].toBool();
    const auto previous=backend.profile;const auto previousChecks=backend.captureConfig.checkRules;std::vector<aero::Signal> retained;QSet<QString> removed;
    QSet<QString> sources;for(const auto &v:(*profile)["signals"].toArray())sources.insert(v.toObject()["source_hash"].toString());for(const auto &v:(*profile)["review"].toArray())sources.insert(v.toObject()["source_hash"].toString());
    for(const auto &s:previous){const auto owners=(*data)["source_owners"].toObject()[QString::fromStdString(s.sourceHash)].toArray();const bool explicitOwner=s.icdSetId==set.toStdString()&&s.icdRevision==hash.toStdString();const bool legacy=s.icdSetId.empty()&&sources.contains(QString::fromStdString(s.sourceHash))&&owners.size()==1&&owners[0].toString()==set;
      if(!restoring&&(explicitOwner||legacy))removed.insert(QString::fromStdString(s.id));else retained.push_back(s);
    }
    auto affected=removed.values();std::sort(affected.begin(),affected.end());
    if(!restoring&&QMessageBox::question(dialog,"Archive ICD",QString("Archive %1?\n\n%2 active mappings will be deactivated. Evidence, chats, and saved recording profiles are retained.\n\n%3").arg(doc["name"].toString()).arg(removed.size()).arg(affected.mid(0,12).join('\n')))!=QMessageBox::Yes)return;
    if(!removed.empty()){
      auto &checks=backend.captureConfig.checkRules;checks.erase(std::remove_if(checks.begin(),checks.end(),[&](const auto &r){return removed.contains(QString::fromStdString(r.signalId));}),checks.end());
      const auto error=publishProfile(retained);if(!error.isEmpty()){backend.captureConfig.checkRules=previousChecks;note->setText(error);return;}
    }
    archive->setEnabled(false);
    setProperty("icdArchivePending",true);
    desktop::libraryJob(this,{{"op",restoring?"library_restore":"library_archive"},{"document_hash",hash}},[this,guard=QPointer<QDialog>(dialog),note,refresh,previous,previousChecks,removed,restoring,hash](QJsonObject,QString error){
      setProperty("icdArchivePending",false);
      if(!error.isEmpty()){if(!removed.empty()){backend.captureConfig.checkRules=previousChecks;const auto rollback=publishProfile(previous);if(!rollback.isEmpty())error+="; profile rollback failed: "+rollback;}if(guard)note->setText(error);else toast(error);return;}
      if(!restoring&&(workspaceDocument["hash"].toString()==hash || (workspaceDocument["icd_revision"].toString().isEmpty()&&QFileInfo(workspacePdf).completeBaseName()==hash))){rememberIcdDocument({},{});workspaceSubsystem.clear();}
      if(!restoring&&workspaceDocument["icd_revision"].toString()==hash){rememberIcdDocument({},{});workspaceSubsystem.clear();}
      if(guard)(*refresh)();if(page==6||page==7||page==12)navigate(page);toast(restoring?"ICD restored to the library; publish explicitly to activate it.":"ICD archived. Existing recordings and conversations were retained.");
    });
  });
  (*refresh)();dialog->show();
}
