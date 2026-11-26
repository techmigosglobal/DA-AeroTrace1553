#include "main_window.hpp"
#include "services/operator_style.hpp"
#include "services/profile_io.hpp"
#include "services/ui_observer.hpp"
#include "services/document_job.hpp"
#include "widgets/startup_splash.hpp"
#include "ui_document_dialog.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
class AssistantPage final : public QDialog {
public:
  explicit AssistantPage(QWidget *parent):QDialog(parent) {setWindowFlags(Qt::Widget);}
protected:
  void keyPressEvent(QKeyEvent *event) override {QWidget::keyPressEvent(event);}
};

QJsonObject verifyAssistantExample(const QString &question,const std::vector<aero::Signal> &mappings,
                                   const QString &hash,const QString &subsystem,bool simulation) {
  static const QRegularExpression request(
    R"(^convert\s+(.+?)\s+from\s+raw\s+((?:0x)?[0-9a-f]{1,4}(?:[\s,]+(?:0x)?[0-9a-f]{1,4}){0,3})\s*[?]?$)",
    QRegularExpression::CaseInsensitiveOption);
  const auto match=request.match(question.trimmed());
  if(!match.hasMatch())return {};
  const auto name=match.captured(1);
  const aero::Signal *rule=nullptr;
  for(const auto &candidate:mappings) {
    const bool scoped=hash==QString::fromStdString(candidate.icdRevision)||hash==QString::fromStdString(candidate.sourceHash);
    if(hash.isEmpty()||!scoped||(!subsystem.isEmpty()&&subsystem!=QString::fromStdString(candidate.subsystem)))continue;
    if(name.compare(QString::fromStdString(candidate.id),Qt::CaseInsensitive)&&
       name.compare(QString::fromStdString(candidate.sourceSignalId),Qt::CaseInsensitive))continue;
    if(rule)return {{"answer","More than one published field matches. Use its full signal ID."}};
    rule=&candidate;
  }
  if(!rule)return {{"answer","No published field matches that name in the selected ICD. Review and publish its mapping first."}};
  QStringList words=match.captured(2).split(QRegularExpression(R"([\s,]+)"),Qt::SkipEmptyParts);
  if(words.size()!=int((rule->lsb+rule->width+15)/16))return {{"answer","The raw word count does not match the published field."}};
  std::vector<std::uint16_t> raw;
  for(auto word:words) {
    if(word.startsWith("0x",Qt::CaseInsensitive))word.remove(0,2);
    raw.push_back(word.toUShort(nullptr,16));
  }
  const auto record=aero::previewSignalRecord(*rule,raw,rule->channel?rule->channel:1,rule->bus<0?0:rule->bus);
  const auto value=aero::decode(record,*rule,simulation);
  if(!value)return {{"answer","The native decoder rejected this raw sample."}};
  QJsonArray citations{QJsonObject{{"kind","native-preview"},{"document","Published mapping · native decoder"},{"page",0}}};
  if(rule->sourcePage&&!rule->sourceHash.empty())
    citations.append(QJsonObject{{"kind","pdf-definition"},{"document",QString::fromStdString(rule->sourceDocument)},
      {"hash",QString::fromStdString(rule->sourceHash)},{"page",int(rule->sourcePage)}});
  return {{"answer",QString("Verified by the native decoder. %1: %2 = %3 %4")
      .arg(name,words.join(' '),QString::number(*value,'g',15),QString::fromStdString(rule->unit))},
      {"citations",citations}};
}

QString documentStore() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents";
}
QString profilePath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/active-profile.json";
}
QString mappingDraftDirectory(){return documentStore()+"/mapping-drafts";}
bool writeMappingDraft(const QString &path,const QJsonObject &draft) {
  QSaveFile output(path);
  const auto bytes=QJsonDocument(draft).toJson();
  return output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit();
}
QString conversationHtml(const QJsonArray &history,int tableTurn=-1,int tablePage=0) {
  QString html="<style>body{font-family:Segoe UI;color:#25372f} .card{margin:12px;padding:12px} th{background:#edf4f0} td,th{padding:6px;text-align:left} a{color:#287355}</style>";
  for(int i=0;i<history.size();++i) {
    const auto turn=history[i].toObject(), result=turn["result"].toObject();
    const bool user=turn["role"].toString()=="user";
    html+=QString("<a name='turn-%1' href='turn://%1' style='color:inherit;text-decoration:none'><div class='card'><b>").arg(i)+QString(user?"You":"AeroTrace Assistant")+"</b><p>"+turn["content"].toString().toHtmlEscaped().replace("\n","<br>")+"</p>";
    const auto rows=result["table"].toObject()["rows"].toArray();
    if(!rows.empty()) {
      const int pages=(rows.size()+12)/13,page=i==tableTurn?std::clamp(tablePage,0,pages-1):0;
      html+=QString("<b>%1 results · Page %2 of %3</b><table width='100%' cellspacing='0'><tr><th>Signal / subsystem</th><th>Words</th><th>Encoding / unit</th></tr>").arg(rows.size()).arg(page+1).arg(pages);
      for(int n=page*13;n<std::min(int(rows.size()),(page+1)*13);++n){const auto row=rows[n].toObject();
        const auto citationIndex=row["citation"].toInt()-1;
        const auto citations=result["citations"].toArray();
        const auto source=citationIndex>=0 && citationIndex<citations.size()?citations[citationIndex].toObject():QJsonObject();
        const auto signalLabel=row["signal"].toString().toHtmlEscaped();
        const auto linked=source.empty()?signalLabel:QString("<a href='icd://%1/%2'>%3</a>").arg(source["hash"].toString()).arg(source["page"].toInt()).arg(signalLabel);
        html+="<tr><td>"+linked+"<br><small>"+row["subsystem"].toString().toHtmlEscaped()+"</small></td><td>"+row["words"].toVariant().toString().toHtmlEscaped()+"</td><td>"+row["encoding"].toString().toHtmlEscaped()+" / "+row["unit"].toString().toHtmlEscaped()+(row["pdf_definition"].toString().isEmpty()?QString():"<br><small>"+row["pdf_definition"].toString().toHtmlEscaped()+"</small>")+"</td></tr>";
      }
      html+="</table>";
      if(page>0)html+=QString("<a href='table://%1/%2'>Previous</a> · ").arg(i).arg(page-1);
      if(page+1<pages)html+=QString("<a href='table://%1/%2'>Next 13 results</a>").arg(i).arg(page+1);
    }
    int citationNumber=0;for(const auto &item:result["citations"].toArray()){++citationNumber;const auto citation=item.toObject();
      if(citation["source_deleted"].toBool()){html+="<p>Source deleted · "+citation["document"].toString().toHtmlEscaped()+"</p>";continue;}if(citation["kind"].toString()=="engineering-reference" || citation["kind"].toString()=="native-preview"){html+="<p><b>"+citation["document"].toString().toHtmlEscaped()+"</b> · local reference</p>";continue;}html+=QString("<p><a href='icd://%1/%2'>[%3] %4 · page %2</a></p>").arg(citation["hash"].toString()).arg(citation["page"].toInt()).arg(citationNumber).arg(citation["document"].toString().toHtmlEscaped());}
    html+="</div></a><hr>";
  }
  return html;
}
}
void Window::persistProfile() {
  QDir().mkpath(QFileInfo(profilePath()).absolutePath());
  QSaveFile output(profilePath());
  if(output.open(QIODevice::WriteOnly)) {
    auto profile=desktop::profileToJson(backend.profile);
    if(!manualRegistry.isEmpty())profile["manual_library"]=manualRegistry;
    output.write(QJsonDocument(profile).toJson());
    if(!output.commit()) toast("Could not persist active ICD profile");
  } else toast("Could not persist active ICD profile");
}
QString Window::publishProfile(const std::vector<aero::Signal> &mappings,const QJsonObject *manual) {
  if(backend.acquisition.active() || (connecting&&connectionIsRecording) || savingSession)
    return "Finish recording and saving before publishing mappings.";
  if(property("icdDeletePending").toBool()&&!icdDeletionApplying)return "Finish the pending ICD deletion before publishing.";
  if(property("icdArchivePending").toBool())return "An ICD archive or restore is still completing. Wait before publishing another profile.";
  if(!manual) {
    const auto manualRules=[](const auto &profile) {
      QMap<QString,QJsonObject> rules;
      for(const auto &signal:profile)if(signal.icdSetId=="manual")
        rules.insert(QString::fromStdString(signal.id),desktop::signalToJson(signal));
      return rules;
    };
    if(manualRules(mappings)!=manualRules(backend.profile))
      return "Use Manual mappings to change validated manual commands.";
  }
  if(auto error=aero::validateProfile(mappings);!error.empty())return QString::fromStdString(error);
  if(auto error=aero::validateChecks(backend.captureConfig.checkRules,mappings);!error.empty())return QString::fromStdString(error);
  const auto path=profilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile output(path);
  auto next=desktop::profileToJson(mappings);
  const auto registry=manual?*manual:manualRegistry;
  if(!registry.isEmpty())next["manual_library"]=registry;
  const auto bytes=QJsonDocument(next).toJson();
  if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())
    return "Could not save active ICD profile: "+output.errorString();
  auto previous=desktop::profileToJson(backend.profile);
  if(!manualRegistry.isEmpty())previous["manual_library"]=manualRegistry;
  if(auto error=backend.applyProfile(mappings);!error.empty()) {
    QSaveFile restore(path);
    const auto old=QJsonDocument(previous).toJson();
    if(!restore.open(QIODevice::WriteOnly) || restore.write(old)!=old.size() || !restore.commit())
      return "Publication failed and the previous profile could not be restored on disk: "+QString::fromStdString(error);
    return QString::fromStdString(error);
  }
  manualRegistry=registry;
  if(auto live=findChild<QDialog *>("liveEngineeringDialog"))live->close();
  applicationEvent("profile_activated","",{{"signals",int(mappings.size())},
    {"profile_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}});
  return {};
}
void Window::activateProfile(const QJsonObject &object) {
  std::vector<aero::Signal> mappings;
  if(auto error=desktop::profileFromJson(object,mappings);!error.isEmpty()) {toast(error);return;}
  if(auto error=publishProfile(mappings);!error.isEmpty()) {toast(error);return;}
  eventText=QString("Activated %1 validated ICD signals").arg(backend.profile.size());
  if(unlocked) navigate(12);
  toast(eventText);
}
void Window::closeDocumentCenter() {
  if(auto assistant=documentDialog.data()) {
    assistant->setAttribute(Qt::WA_DeleteOnClose,false);
    assistant->reject();
    delete assistant;
  }
}

void Window::documentCenter(QString pdf) {
  if(!unlocked)return;
  if(property("icdDeletePending").toBool()){toast("Finish the pending ICD deletion before opening the Assistant.");return;}
  const auto showAssistant=[this] {
    workspaceStack->setCurrentWidget(documentDialog);
    updateShellHeading(-1,"AeroTrace Assistant","Ask about indexed ICDs and inspect cited PDF pages.");
  };
  if(documentDialog) {
    showAssistant();
    if(!pdf.isEmpty()) {
      if(documentDialog->property("jobRunning").toBool()) {toast("Finish or cancel the current ICD job before opening another PDF.");return;}
      documentDialog->findChild<QLineEdit *>("pdfPath")->setText(pdf);documentDialog->findChild<QPushButton *>("importButton")->click();
    }
    return;
  }
  // The assistant stays alive when another route is visible. PDF and OCR
  // remain in the document worker, with one job at a time per assistant.
  auto dialog=new AssistantPage(workspaceStack);
  documentDialog=dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  Ui::DocumentDialog form;
  form.setupUi(dialog);
  form.askButton->setProperty("primary",true);
  desktop::actionIcon(form.askButton,"send",true);
  desktop::actionIcon(form.cancelButton,"cancel");
  desktop::actionIcon(form.browseButton,"folder");
  desktop::actionIcon(form.importButton,"add-file",true);
  desktop::actionIcon(form.chatOpenSourceButton,"file");
  desktop::actionIcon(form.activateButton,"edit");
  desktop::actionIcon(form.explainEvidence,"info");
  desktop::actionIcon(form.openPageButton,"file");
  desktop::actionIcon(form.previewAction,"search");
  desktop::actionIcon(form.applyAction,"check");
  dialog->setObjectName("assistantPage");
  dialog->setAccessibleName("AeroTrace Assistant");
  dialog->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
  auto rootLayout=form.dialogLayout;
  auto shell=new QHBoxLayout;rootLayout->addLayout(shell);
  auto historyPanel=new QWidget(dialog);historyPanel->setObjectName("conversationSidebar");historyPanel->setMinimumWidth(170);historyPanel->setMaximumWidth(250);auto historyLayout=new QVBoxLayout(historyPanel);
  auto newChat=new QPushButton("New chat",historyPanel);newChat->setObjectName("newConversation");desktop::actionIcon(newChat,"add-file");historyLayout->addWidget(newChat);
  auto search=new QLineEdit(historyPanel);search->setObjectName("conversationSearch");search->setPlaceholderText("Search conversations");historyLayout->addWidget(search);
  auto threads=new QListWidget(historyPanel);threads->setObjectName("conversationList");historyLayout->addWidget(threads,1);
  auto older=new QPushButton("Load older messages",historyPanel);older->setObjectName("olderMessages");historyLayout->addWidget(older);
  auto body=new QWidget(dialog);auto bodyLayout=new QVBoxLayout(body);
  // Move the Designer contents into the conversation column, retaining object names.
  std::function<void(QLayout *)> reparentContents=[&](QLayout *layout){for(int i=0;i<layout->count();++i){auto item=layout->itemAt(i);if(auto widget=item->widget()){if(widget->parentWidget()==dialog)widget->setParent(body);}else if(auto child=item->layout())reparentContents(child);}};
  while(rootLayout->count()>1){auto item=rootLayout->takeAt(0);if(auto widget=item->widget()){bodyLayout->addWidget(widget);delete item;}else if(auto child=item->layout()){reparentContents(child);child->setParent(nullptr);bodyLayout->addLayout(child);}else bodyLayout->addItem(item);}
  shell->addWidget(historyPanel);shell->addWidget(body,1);form.dialogLayout=bodyLayout;
  form.explanation->hide();
  form.pdfPath->setText(pdf);
  form.activateButton->setText("Review signals…");
  form.modelUrl->setText("Managed authenticated loopback");form.modelUrl->setReadOnly(true);
  form.modelName->setText("AeroTrace Assistant · CPU only");form.modelName->setReadOnly(true);
  form.cancelButton->setEnabled(false);
  form.askButton->setEnabled(false);form.documentSelector->addItem("Choose an indexed ICD…",QString());
  form.progress->setProperty("operatorBusy",true);
  form.chatOpenSourceButton->setEnabled(false);form.openPageButton->setEnabled(false);form.progress->setTextVisible(false);form.progress->setFormat(QString());form.progress->setMaximumHeight(6);form.dialogLayout->setSpacing(8);
  form.documentSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);form.documentSelector->setMinimumContentsLength(24);
  form.answer->setFont(QApplication::font());form.question->setMaxLength(2000);form.question->setToolTip("Ask: What is TEMP's scale?");
  form.actionQuestion->setMaxLength(2000);
  form.mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  form.mappingTable->horizontalHeader()->setStretchLastSection(true);
  struct Job {
    QPointer<QProcess> process;
    QByteArray buffer;
    QJsonObject profile;
    QJsonObject proposal,base;
    QString original;
    QString citationPdf;
    QStringList files;
    QString setName;
    QString fileLabel;
    QString hash;
    QString conversation;
    QString conversationScope,conversationSource;
    QString pendingQuestion;
    QJsonArray history;
    unsigned citationPage{1};
    QString id;QElapsedTimer elapsed;
    bool completed{},cancelled{};
    QJsonObject prepared;
    QList<QJsonObject> deferred;
    QString draftPath;
  };
  auto job=std::make_shared<Job>();
  const auto sourcePdf=[](const QString &original,const QString &hash) {
    static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    return !original.isEmpty() && digest.match(hash).hasMatch()
      ? QFileInfo(original).dir().filePath(hash+".pdf") : original;
  };
  const auto canAsk=[form,job,dialog] {
    return !dialog->property("assistantSourceDeleted").toBool() && (form.answerSource->currentIndex()==1 || !job->hash.isEmpty());
  };
  auto scope=new QComboBox(dialog);scope->setObjectName("assistantScope");scope->addItem(workspaceSubsystem.isEmpty()?"All subsystems":workspaceSubsystem,workspaceSubsystem);if(!workspaceSubsystem.isEmpty())scope->addItem("All subsystems",QString());
  auto tools=new QHBoxLayout;
  tools->addWidget(scope,1);
  form.dialogLayout->insertLayout(2,tools);
  auto composer=new QPlainTextEdit(dialog);composer->setObjectName("chatComposer");composer->setPlaceholderText("Ask about this ICD…");
  composer->setToolTip("For a published field, try Convert Temperature from raw 0064.");composer->setMaximumHeight(100);composer->setMinimumHeight(68);composer->setAccessibleName("Message AeroTrace Assistant");
  form.question->hide();form.questionLayout->insertWidget(0,composer,1);
  connect(composer,&QPlainTextEdit::textChanged,dialog,[composer,form]{const auto text=composer->toPlainText().left(2000);if(form.question->text()!=text)form.question->setText(text);});
  connect(form.question,&QLineEdit::textChanged,dialog,[composer](const QString &text){if(composer->toPlainText()!=text){const QSignalBlocker block(composer);composer->setPlainText(text);}});

  form.questionLayout->removeWidget(form.answerSource);tools->insertWidget(0,form.answerSource);
  auto deferAnswer=new QPushButton("Ask after recording",dialog);
  desktop::actionIcon(deferAnswer,"pause");
  deferAnswer->setObjectName("deferIcdAnswer");
  form.questionLayout->addWidget(deferAnswer);
  deferAnswer->hide();
  auto proposeFromChat=new QPushButton("Propose mapping",dialog);
  desktop::actionIcon(proposeFromChat,"mapping");
  proposeFromChat->setObjectName("proposeMappingFromChat");
  form.questionLayout->addWidget(proposeFromChat);
  form.previewAction->setText("Propose mapping");
  form.applyAction->setText("Review draft…");
  form.fileLayout->removeWidget(form.cancelButton);form.questionLayout->addWidget(form.cancelButton);form.cancelButton->setText("Cancel");
  form.answerSource->setItemText(0,"PDF evidence");
  auto answerMode=new QComboBox(dialog);
  answerMode->setObjectName("assistantAnswerMode");answerMode->setAccessibleName("Assistant answer mode");
  answerMode->addItems({"Cited lookup","Local RAG"});
  const auto rememberedPack=settings.value("localAssistantPack").toString();
  if(!rememberedPack.isEmpty())inference.selectAssets(rememberedPack);
  const bool modelPresent=inference.assetsPresent();
  if(auto model=qobject_cast<QStandardItemModel *>(answerMode->model())) {
    model->item(1)->setEnabled(modelPresent);
    model->item(1)->setToolTip(modelPresent?"Explain retrieved evidence with the local model":
      "Install the optional local model pack to enable generated answers.");
  }
  tools->addWidget(answerMode);
  auto chooseModel=new QPushButton("Local model pack…",dialog);
  chooseModel->setObjectName("selectAssistantPack");
  desktop::actionIcon(chooseModel,"folder");
  tools->addWidget(chooseModel);
  connect(chooseModel,&QPushButton::clicked,dialog,[this,dialog,form,answerMode] {
    if(dialog->property("jobRunning").toBool()){form.status->setText("Wait for the current job to finish.");return;}
    const auto path=QFileDialog::getExistingDirectory(dialog,"Choose the optional assistant-runtime folder",
        settings.value("localAssistantPack").toString());
    if(path.isEmpty())return;
    if(!inference.selectAssets(path)){form.status->setText("Choose the assistant-runtime folder from the verified local model pack.");return;}
    settings.setValue("localAssistantPack",path);
    if(auto model=qobject_cast<QStandardItemModel *>(answerMode->model()))model->item(1)->setEnabled(true);
    form.status->setText("Local RAG is available. The runtime and model are checked before use.");
  });
  auto evidence=new QTextBrowser(dialog);
  evidence->setObjectName("assistantSources");evidence->setAccessibleName("Assistant cited sources");
  evidence->setOpenLinks(false);evidence->setOpenExternalLinks(false);
  evidence->setMinimumWidth(240);evidence->setMaximumWidth(360);evidence->hide();
  auto showSources=new QPushButton("Sources",dialog);showSources->setCheckable(true);
  desktop::actionIcon(showSources,"file");tools->addWidget(showSources);
  shell->addWidget(evidence,0);
  connect(showSources,&QPushButton::toggled,evidence,&QWidget::setVisible);
  evidence->setHtml("<h3>Sources</h3><p>Ask a question about the selected ICD.</p>");
  connect(evidence,&QTextBrowser::anchorClicked,form.answer,&QTextBrowser::anchorClicked);
  form.workspaceTabs->setTabVisible(3,false);
  auto diagnostics=new QToolButton(dialog);diagnostics->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);diagnostics->setText("Offline model diagnostics");diagnostics->setCheckable(true);diagnostics->setArrowType(Qt::RightArrow);form.dialogLayout->addWidget(diagnostics);
  form.workspaceTabs->removeTab(3);form.modelTab->setParent(dialog);form.dialogLayout->addWidget(form.modelTab);form.modelTab->hide();
  connect(diagnostics,&QToolButton::toggled,dialog,[form,diagnostics](bool show){form.modelTab->setVisible(show);diagnostics->setArrowType(show?Qt::DownArrow:Qt::RightArrow);});
  form.localModel->setTitle("AeroTrace Assistant · 20% CPU cap · 2.5 GiB memory cap");
  form.workspaceTabs->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);form.dialogLayout->setStretch(form.dialogLayout->indexOf(form.workspaceTabs),1);
  form.answer->setOpenLinks(false);form.answer->setOpenExternalLinks(false);
  connect(form.answer,&QTextBrowser::anchorClicked,dialog,[form,job,sourcePdf](const QUrl &url){
    if(url.scheme()=="table"){form.answer->setHtml(conversationHtml(job->history,url.host().toInt(),url.path().mid(1).toInt()));form.answer->scrollToAnchor("turn-"+url.host());return;}
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    if(url.scheme()=="icd" && digest.match(url.host()).hasMatch() && url.path().mid(1).toUInt()>0){const auto path=sourcePdf(job->original,url.host());if(QFileInfo::exists(path)){auto file=QUrl::fromLocalFile(path);file.setFragment("page="+url.path().mid(1));QDesktopServices::openUrl(file);}}
  });
  auto suggestions=new QHBoxLayout;form.dialogLayout->insertLayout(3,suggestions);
  for(const auto &question:QStringList{"Which words are float?","Show unresolved fields"}){auto button=new QPushButton(question,dialog);suggestions->addWidget(button);connect(button,&QPushButton::clicked,dialog,[form,question]{form.question->setText(question);form.askButton->click();});}
  auto manage=new QToolButton(dialog);manage->setObjectName("manageIcd");manage->setMinimumWidth(0);manage->setText("Documents");manage->setCheckable(true);manage->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);manage->setArrowType(Qt::RightArrow);form.dialogLayout->insertWidget(5,manage);
  const auto manageFile=[form](bool visible){for(int i=0;i<form.fileLayout->count();++i)if(auto widget=form.fileLayout->itemAt(i)->widget())widget->setVisible(visible);};
  form.workspaceTabs->setTabVisible(1,false);form.workspaceTabs->setTabVisible(2,false);form.workspaceTabs->tabBar()->hide();
  connect(manage,&QToolButton::toggled,dialog,[form](bool show){form.workspaceTabs->setTabVisible(1,show);form.workspaceTabs->setTabVisible(2,show);form.workspaceTabs->tabBar()->setVisible(show);});
  manageFile(false);connect(manage,&QToolButton::toggled,dialog,[manage,manageFile](bool visible){manage->setArrowType(visible?Qt::DownArrow:Qt::RightArrow);manage->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);manage->setText("Documents");manageFile(visible);});
  form.dialogLayout->removeWidget(manage);form.libraryLayout->addWidget(manage);
  form.dialogLayout->removeWidget(diagnostics);historyLayout->addWidget(diagnostics);
  const bool legacyFixture=qApp->property("legacyIcdFixture").toBool();
  if(!legacyFixture) {
    form.activateButton->hide();form.answerSource->hide();
    deferAnswer->hide();diagnostics->hide();form.workspaceTabs->setTabVisible(2,false);
  }
  auto runner=std::make_shared<std::function<void(QJsonObject)>>();
  const std::weak_ptr<std::function<void(QJsonObject)>> weakRunner=runner;
  *runner=[this,dialog,form,job,canAsk,sourcePdf,scope,weakRunner,threads,newChat,older,legacyFixture,evidence](QJsonObject request) {
    if(property("icdDeletePending").toBool()){form.status->setText("Finish the pending ICD deletion.");return;}
    applicationEvent("document_requested",request["op"].toString()+" "+QFileInfo(request["pdf"].toString()).fileName());
    if(job->process || (dialog->property("jobRunning").toBool()&&request["op"]!="explain")) {form.status->setText("One document job is already running. Cancel it before starting another.");return;}
    const bool duringRecording=backend.acquisition.active() || (connecting&&connectionIsRecording) || savingSession;
    const bool defer=request["op"]=="ingest" || request["defer_generation"].toBool();
    if(duringRecording&&defer) {
      if(job->deferred.size()>=8){form.status->setText("Eight ICD jobs are already queued.");return;}
      if(request["op"]=="ask") {
        if(job->hash.isEmpty() || request["question"].toString().trimmed().isEmpty()){form.status->setText("Select an indexed ICD and enter a question.");return;}
        request["queued_document"]=job->hash;request["queued_scope"]=scope->currentData().toString();
      }
      job->deferred.append(request);dialog->setProperty("queuedIcdJobs",job->deferred.size());form.cancelButton->setEnabled(true);
      form.status->setText(QString("%1 ICD job(s) queued until recording and saving finish. Indexed questions are available now.").arg(job->deferred.size()));
      return;
    }
    if(request.contains("queued_document")&&(request["queued_document"]!=job->hash || request["queued_scope"]!=scope->currentData().toString())){
      form.status->setText("The selected ICD or subsystem changed. Re-enter the deferred question in the desired scope.");
      return;
    }
    if(request["op"]=="ask" || request["op"]=="explain"){
      if(request["op"]=="ask" && !job->conversation.isEmpty() && (job->conversationScope!=scope->currentData().toString() || job->conversationSource!=(form.answerSource->currentIndex()==1?"published":"documents"))){job->conversation.clear();job->history={};form.answer->clear();}
      request["conversation_id"]=request["op"]=="explain"?request["prepared"].toObject()["conversation_id"]:QJsonValue(job->conversation);
      if(request["op"]=="ask")request["native_preview"]=property("nativeConversionPreview").toString();
    }
    if(request["op"]=="conversation_new"){
      request["document_hash"]=job->hash.isEmpty()||dialog->property("assistantSourceDeleted").toBool()?
        workspaceDocument["icd_revision"].toString():job->hash;request["subsystem"]=workspaceSubsystem;request["answer_source"]=form.answerSource->currentIndex()==1?"published":"documents";}
    job->id=QUuid::createUuid().toString(QUuid::Id128);job->elapsed.start();
    applicationEvent("document_job_start",request["op"].toString(),{{"job_id",job->id},{"document_hash",job->hash}});
    job->proposal={};job->draftPath.clear();form.actionPreview->clear();form.applyAction->setEnabled(false);
    if(request["op"].toString()=="propose") {
      if(dialog->property("documentArchived").toBool()){form.status->setText("Restore this archived ICD in Manage ICDs before reviewing mapping changes.");return;}
      if(job->hash.isEmpty()){form.status->setText("Select an indexed ICD first");return;}
      job->base=desktop::profileToJson(backend.mappings());request["active_profile"]=job->base;request["document_hash"]=job->hash;
    }
    if(request["op"].toString()=="ask") {
      const bool published=request["answer_source"].toString()=="published";
      if((!published && job->hash.isEmpty()) || request["question"].toString().trimmed().isEmpty()) {form.status->setText("Select an answer source and enter a question.");return;}
      if(published) request["active_profile"]=desktop::profileToJson(backend.mappings());
      if(dialog->property("assistantSourceDeleted").toBool()){form.status->setText("Choose an indexed revision to ask a new question.");return;}
      const bool generation=request["defer_generation"].toBool()&&!published;
      request["subsystem"]=scope->currentData().toString();request["prepare_generation"]=generation;request["fast_only"]=!generation;
      job->cancelled=false;dialog->setProperty("lastAnswerMode",QString());dialog->setProperty("answerDeltaCount",0);
      request["document_hash"]=job->hash;
      const auto verified=verifyAssistantExample(request["question"].toString(),backend.profile,job->hash,
          scope->currentData().toString(),!backend.captureConfig.hardware);
      if(!verified.isEmpty())request["native_example"]=verified;
      job->pendingQuestion=request["question"].toString();form.answer->append("<p><b>You</b><br>"+job->pendingQuestion.toHtmlEscaped()+"</p><p>Searching the selected scope…</p>");
    }
    if(request["op"].toString()=="ingest") form.scopeLabel->setText("Indexing the selected ICD…");
    QString python=QCoreApplication::applicationDirPath()+"/document-runtime/python.exe";
    const bool deployed=QFileInfo::exists(QCoreApplication::applicationDirPath()+"/deployment.json");
    if(!QFileInfo::exists(python) && !deployed) python=QDir(qEnvironmentVariable("ProgramFiles","C:/Program Files")).filePath("AeroTrace1553/document-runtime/python.exe");
    if(!QFileInfo::exists(python) && !deployed) python=QString::fromUtf8(AEROTRACE_DOCUMENT_PYTHON);
    QString script=QCoreApplication::applicationDirPath()+"/document-worker/worker.py";
    if(!QFileInfo::exists(script) && !deployed) script=QString::fromUtf8(AEROTRACE_DOCUMENT_WORKER);
    if(!QFileInfo::exists(python) || !QFileInfo::exists(script)) {
      if(request["op"].toString()=="explain")inference.release();
      form.status->setText("Document runtime is missing. Run scripts/setup-documents.ps1, then package again.");return;
    }
    request["store"]=documentStore();
    settings.setValue("localModelUrl",form.modelUrl->text());settings.setValue("localModelName",form.modelName->text());
    auto process=new QProcess(dialog);job->process=process;job->buffer.clear();job->completed=false;
    dialog->setProperty("jobRunning",true);
    dialog->setProperty("heavyIcdJob",request["op"]=="ingest" || request["op"]=="explain" ||
      (request["op"]=="ask"&&request["prepare_generation"].toBool()));
#ifdef Q_OS_WIN
    auto resourceJob=std::shared_ptr<void>(CreateJobObjectW(nullptr,nullptr),[](void *handle) {if(handle) CloseHandle(handle);});
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit=1024ull*1024*1024;
    if(!resourceJob || !SetInformationJobObject(resourceJob.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
      form.status->setText("Cannot establish the document worker's memory limit.");job->process=nullptr;dialog->setProperty("jobRunning",false);process->deleteLater();return;
    }
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};cpu.ControlFlags=JOB_OBJECT_CPU_RATE_CONTROL_ENABLE|JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;cpu.CpuRate=2000;
    SetInformationJobObject(resourceJob.get(),JobObjectCpuRateControlInformation,&cpu,sizeof(cpu));
    connect(process,&QProcess::started,dialog,[process,resourceJob,form] {
      HANDLE child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE|PROCESS_SET_INFORMATION,FALSE,DWORD(process->processId()));
      const bool assigned=child && AssignProcessToJobObject(resourceJob.get(),child);
      // A hard CPU cap already reserves acquisition headroom. Below-normal
      // priority can starve startup indefinitely on a busy workstation.
      if(child) {SetPriorityClass(child,NORMAL_PRIORITY_CLASS);CloseHandle(child);}
      if(!assigned) {form.status->setText("Cannot enforce document worker limits; job stopped.");process->kill();}
    });
#endif
    auto environment=QProcessEnvironment::systemEnvironment();environment.insert("PYTHONUTF8","1");environment.insert("PYTHONUNBUFFERED","1");
    environment.insert("PYTHONDONTWRITEBYTECODE","1");environment.insert("PYTHONNOUSERSITE","1");environment.remove("PYTHONHOME");environment.remove("PYTHONPATH");
    environment.insert("OPENBLAS_NUM_THREADS","1");environment.insert("OMP_NUM_THREADS","1");environment.insert("MKL_NUM_THREADS","1");
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    form.importButton->setEnabled(false);form.askButton->setEnabled(false);form.cancelButton->setEnabled(true);form.activateButton->setEnabled(false);
    form.browseButton->setEnabled(false);form.documentSelector->setEnabled(false);form.question->setEnabled(false);form.chatOpenSourceButton->setEnabled(false);form.openPageButton->setEnabled(false);
    threads->setEnabled(false);newChat->setEnabled(false);older->setEnabled(false);scope->setEnabled(false);form.answerSource->setEnabled(false);
    form.progress->setRange(0,desktop::startupMotionEnabled()?0:100);
    form.progress->setValue(0);
    form.status->setText(request["op"]=="ingest"?"Reading the ICD PDF locally…":
      request["op"]=="ask"?"Searching indexed ICD evidence…":"Loading the ICD library…");
    connect(process,&QProcess::readyReadStandardError,dialog,[process] {applicationEvent("document_stderr",QString::fromUtf8(process->readAllStandardError()));});
    connect(process,&QProcess::readyReadStandardOutput,dialog,[this,dialog,process,job,form,sourcePdf,threads,scope,evidence] {
      job->buffer+=process->readAllStandardOutput();
      if(job->buffer.size()>4*1024*1024) {form.status->setText("Document worker exceeded its response limit");process->kill();return;}
      while(job->buffer.contains('\n')) {
        const auto end=job->buffer.indexOf('\n');const auto line=job->buffer.left(end);job->buffer.remove(0,end+1);
        const auto message=QJsonDocument::fromJson(line).object();
        const auto event=message["event"].toString();
        if(event=="error" || event=="progress" || event=="done")applicationEvent("document_"+event,message["message"].toString(),{{"job_id",job->id},{"duration_ms",job->elapsed.elapsed()},{"page",message["page"]},{"document_hash",message["document_hash"]},{"mode",message["mode"]}});
        if(event=="progress") {
          form.progress->setRange(0,message["total"].toInt());form.progress->setValue(message["page"].toInt());
          form.status->setText(QString("Page %1/%2 · %3 · %4 mapped · %5 need review").arg(message["page"].toInt()).arg(message["total"].toInt()).arg(message["method"].toString()).arg(message["mapped"].toInt()).arg(message["review"].toInt()));
        } else if(event=="answer_delta") {dialog->setProperty("answerDeltaCount",dialog->property("answerDeltaCount").toInt()+1);form.answer->moveCursor(QTextCursor::End);form.answer->insertPlainText(message["text"].toString());}
        else if(event=="error") {job->completed=true;form.status->setText(message["message"].toString());if(!job->pendingQuestion.isEmpty()) {form.answer->append("No answer saved: "+message["message"].toString());job->pendingQuestion.clear();}}
        else if(event=="done") {
          job->completed=true;
          if(message.contains("conversation_id")){job->conversation=message["conversation_id"].toString();dialog->setProperty("activeConversation",job->conversation);}
          if(message["needs_generation"].toBool()){job->prepared=message;form.status->setText("Preparing offline explanation…");continue;}
          if(message.contains("document_hash")) {job->hash=message["document_hash"].toString();if(!job->hash.isEmpty()) settings.setValue("icdChatDocument",job->hash);}
          if(message.contains("conversations")) {
            const QSignalBlocker block(threads);threads->clear();
            for(const auto &v:message["conversations"].toArray()){const auto thread=v.toObject();auto item=new QListWidgetItem((thread["archived"].toBool()?"[Archived] ":"")+thread["title"].toString(),threads);item->setData(Qt::UserRole,thread);item->setToolTip(thread["subsystem"].toString()+" · "+thread["answer_source"].toString());if(thread["id"].toString()==job->conversation)threads->setCurrentItem(item);}
          }
          if(message.contains("subsystem")){const QSignalBlocker block(scope);scope->clear();scope->addItem(message["subsystem"].toString().isEmpty()?"All subsystems":message["subsystem"].toString(),message["subsystem"]);}
          if(message.contains("answer_source")){const QSignalBlocker block(form.answerSource);form.answerSource->setCurrentIndex(message["answer_source"]=="published"?1:0);}
          job->conversationScope=scope->currentData().toString();job->conversationSource=form.answerSource->currentIndex()==1?"published":"documents";
          if(message.contains("documents")) {
            const QSignalBlocker block(form.documentSelector);form.documentSelector->clear();form.documentSelector->addItem("Choose an indexed ICD…",QString());
            for(const auto &entry:message["documents"].toArray()) {const auto doc=entry.toObject();form.documentSelector->addItem(doc["name"].toString()+" · "+doc["hash"].toString().left(8),doc["hash"].toString());}
            if(message["archived"].toBool()&&form.documentSelector->findData(job->hash)<0)form.documentSelector->addItem("Archived · "+message["profile"].toObject()["document"].toString()+" · "+job->hash.left(8),job->hash);
            form.documentSelector->setCurrentIndex(std::max(0,form.documentSelector->findData(job->hash)));
          }
          if(message.contains("source_deleted")) {
            dialog->setProperty("assistantSourceDeleted",message["source_deleted"].toBool());
            if(message["source_deleted"].toBool()) {
              job->original.clear();job->profile={};
              form.scopeLabel->setText("Source deleted · saved conversation");
            }
          } else if(message["library"].toBool())dialog->setProperty("assistantSourceDeleted",false);
          if(message.contains("archived"))dialog->setProperty("documentArchived",message["archived"].toBool());
          if(message.contains("pdf_path")) {job->original=message["pdf_path"].toString();if(message["library"].toBool()) form.pdfPath->clear();}
          if(message.contains("message_changed")) {
            const auto changed=message["message_id"].toVariant().toLongLong();
            for(int i=0;i<job->history.size();++i) {
              if(job->history[i].toObject()["message_id"].toVariant().toLongLong()==changed) {job->history.removeAt(i);break;}
            }
            form.answer->setHtml(conversationHtml(job->history));
            form.status->setText("Conversation message updated.");
          }
          if(message.contains("history")) {if(message["append_history"].toBool()){auto previous=message["history"].toArray();for(const auto &turn:job->history)previous.append(turn);job->history=previous;}else job->history=message["history"].toArray();job->pendingQuestion.clear();form.answer->setHtml(conversationHtml(job->history));form.answer->scrollToAnchor(message["append_history"].toBool()?"turn-0":"turn-"+QString::number(job->history.size()-1));}
          const auto citations=message["citations"].toArray();
          if(message.contains("answer")||message.contains("history")) {
            auto sourceRows=citations;
            if(sourceRows.isEmpty()&&!job->history.isEmpty())
              sourceRows=job->history.last().toObject()["result"].toObject()["citations"].toArray();
            QString cards="<h3>Sources</h3>";
            if(sourceRows.isEmpty())cards+="<p>No supporting passage found for this answer.</p>";
            for(const auto &value:sourceRows) {
              const auto source=value.toObject();
              const auto title=source["document"].toString().toHtmlEscaped();
              const auto location=source["context"].toString().toHtmlEscaped();
              if(source["source_deleted"].toBool()){cards+="<p>"+title+" · Source deleted</p>";continue;}
              cards+=QString("<p><b>%1</b><br>Page %2 · %3</p><p>%4</p>").arg(title)
                .arg(source["page"].toInt()).arg(location,source["excerpt"].toString().left(600).toHtmlEscaped());
              if(source["page"].toInt()>0&&!source["hash"].toString().isEmpty())
                cards+=QString("<p><a href='icd://%1/%2'>Open page</a></p>").arg(source["hash"].toString()).arg(source["page"].toInt());
            }
            evidence->setHtml(cards);
          }
          const auto citation=citations.isEmpty()?QJsonObject():citations[0].toObject();
          job->citationPage=citation["page"].toInt(1);
          job->citationPdf=citation.isEmpty()?QString():sourcePdf(job->original,citation["hash"].toString());
          form.chatOpenSourceButton->setEnabled(QFileInfo::exists(job->citationPdf));
          if(message.contains("proposal")) {
            job->proposal=message["proposal"].toObject();form.actionPreview->setPlainText(job->proposal["summary"].toString());
            if(job->proposal["kind"]=="mapping") {
              std::vector<aero::Signal> validated;
              const auto error=desktop::profileFromJson(job->proposal["profile"].toObject(),validated);
              if(!error.isEmpty()){form.status->setText(error);job->proposal={};continue;}
              QDir().mkpath(mappingDraftDirectory());
              job->draftPath=mappingDraftDirectory()+"/"+QUuid::createUuid().toString(QUuid::Id128)+".json";
              if(!writeMappingDraft(job->draftPath,{{"document_hash",job->hash},{"before",job->base},{"proposal",job->proposal}})){
                form.status->setText("Could not save the mapping draft.");job->proposal={};continue;
              }
              form.status->setText("Mapping draft saved in the ICD Library. Review and preview it before publishing.");
            } else form.status->setText("Review the proposed action.");
          } else if(message.contains("profile")) {
            job->profile=message["profile"].toObject();job->original=message["pdf_path"].toString();
            if(!message["library"].toBool()){
              const auto replacement=property("icdReplacementSet").toString();
              if(!replacement.isEmpty()){
                job->profile["icd_set_id"]=replacement;
                for(const auto &key:{"signals","review"}){auto rows=job->profile[key].toArray();for(int i=0;i<rows.size();++i){auto row=rows[i].toObject();row["icd_set_id"]=replacement;row["icd_revision"]=job->hash;rows[i]=row;}job->profile[key]=rows;}
                desktop::libraryJob(this,{{"op","library_revision"},{"document_hash",job->hash},{"previous_hash",property("icdReplacementRevision").toString()}},[this](QJsonObject,QString error){if(!error.isEmpty())toast(error);});
                setProperty("icdReplacementSet",QVariant());setProperty("icdReplacementRevision",QVariant());
              }
              if(qApp->property("legacyIcdFixture").toBool())rememberIcdDocument(job->profile,job->original);
            }
            const auto mappings=job->profile["signals"].toArray();const auto review=job->profile["review"].toArray();
            form.mappingTable->setRowCount(mappings.size()+review.size());
            int row=0;
            for(const auto &entry:mappings) {
              const auto mapping=entry.toObject();
              const QStringList cells={mapping["id"].toString(),QString::number(mapping["source_page"].toInt()),"Review required",mapping["evidence"].toObject()["text"].toString()};
              for(int column=0;column<4;++column) form.mappingTable->setItem(row,column,new QTableWidgetItem(cells[column]));
              form.mappingTable->item(row,1)->setData(Qt::UserRole,mapping["source_hash"].toString());
              form.mappingTable->item(row,1)->setToolTip(mapping["source_document"].toString());
              auto item=form.mappingTable->item(row,0);
              if(qApp->property("legacyIcdFixture").toBool()){item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Unchecked);item->setData(Qt::UserRole,mapping.toVariantMap());}
              ++row;
            }
            for(const auto &entry:review) {
              const auto item=entry.toObject();const QStringList cells={item["id"].toString(),QString::number(item["page"].toInt()),"Review",item["reason"].toString()+" · "+item["text"].toString()};
              for(int column=0;column<4;++column) form.mappingTable->setItem(row,column,new QTableWidgetItem(cells[column]));
              form.mappingTable->item(row,1)->setData(Qt::UserRole,item["source_hash"].toString());
              form.mappingTable->item(row,1)->setToolTip(item["source_document"].toString());
              if(qApp->property("legacyIcdFixture").toBool()&&item["candidate"].isObject()) {auto cell=form.mappingTable->item(row,0);cell->setFlags(cell->flags()|Qt::ItemIsUserCheckable);cell->setCheckState(Qt::Unchecked);cell->setData(Qt::UserRole,item["candidate"].toObject().toVariantMap());}
              ++row;
            }
            form.activateButton->setEnabled(form.mappingTable->rowCount()>0);
            const bool imported=!message["library"].toBool();
            if(imported){dialog->setProperty("indexedDocumentHash",job->hash);if(auto manage=dialog->findChild<QToolButton *>("manageIcd"))manage->setChecked(false);}
            const bool active=std::any_of(backend.profile.begin(),backend.profile.end(),[&](const auto &signal){
              const auto hash=QString::fromStdString(signal.sourceHash);
              const auto fromSource=[&](const QJsonValue &entry){return entry.toObject()["source_hash"].toString()==hash;};
              return !hash.isEmpty() && (hash==job->hash || std::any_of(mappings.begin(),mappings.end(),fromSource) ||
                std::any_of(review.begin(),review.end(),fromSource));
            });
            form.scopeLabel->setText(QString("%1 · %2 pages · %3").arg(job->profile["document"].toString()).arg(job->profile["pages"].toInt()).arg(job->hash.left(12)));
            form.workspaceTabs->setTabText(1,"PDF evidence");
            form.status->setText(imported?"PDF indexed. Ask a question or open the source evidence.":"PDF evidence loaded.");
            if(imported&&desktop::startupMotionEnabled()){
              auto effect=new QGraphicsOpacityEffect(form.status);
              form.status->setGraphicsEffect(effect);
              auto fade=new QPropertyAnimation(effect,"opacity",effect);
              fade->setDuration(desktop::operatorTransitionMs);fade->setStartValue(0.55);fade->setEndValue(1.0);
              fade->setEasingCurve(QEasingCurve::OutCubic);fade->start(QAbstractAnimation::DeleteWhenStopped);
            }
          } else if(message.contains("answer")) {
            if(!message.contains("history")) {
              if(!job->pendingQuestion.isEmpty())job->history.append(QJsonObject{{"role","user"},{"content",job->pendingQuestion}});
              job->history.append(QJsonObject{{"role","assistant"},{"content",message["answer"]},{"result",message}});job->pendingQuestion.clear();form.answer->setHtml(conversationHtml(job->history));
            }
            dialog->setProperty("lastAnswerMode",message["mode"]);dialog->setProperty("lastCitationCount",message["citations"].toArray().size());
            const auto mode=message["mode"].toString();
            form.status->setText(mode=="local-model" ? "Local RAG · checked against cited evidence." :
              message.contains("generation_note") ? "Cited lookup · local generation unavailable." :
              mode=="published-mapping" ? "Published mapping used by Engineering." :
              mode=="clarification" ? "Specify a subsystem, message or route." :
              mode=="draft-layout" ? "PDF layout evidence. Review missing conversion fields before publishing." :
              "ICD document evidence. Check the cited source pages.");
          } else if(message["library"].toBool()) {
            job->hash.clear();form.mappingTable->setRowCount(0);form.scopeLabel->setText("No indexed ICD selected · conversion profile unchanged");form.status->setText("Import a PDF to start its own conversation.");
          }
        }
      }
    });
    connect(process,&QProcess::errorOccurred,dialog,[this,dialog,form,job,process,canAsk,operation=request["op"].toString(),threads,newChat,older,scope](QProcess::ProcessError error) {
      applicationEvent("document_process_error",process->errorString());
      form.status->setText("Could not run document worker. Check the installed document runtime.");
      if(error==QProcess::FailedToStart) {
        if(operation=="explain")inference.release();
        threads->setEnabled(true);newChat->setEnabled(true);older->setEnabled(true);scope->setEnabled(true);form.answerSource->setEnabled(true);
      form.importButton->setEnabled(true);form.askButton->setEnabled(canAsk());form.cancelButton->setEnabled(false);form.browseButton->setEnabled(true);form.documentSelector->setEnabled(true);form.question->setEnabled(true);dialog->setProperty("jobRunning",false);
        form.progress->setRange(0,100);form.progress->setValue(0);job->process=nullptr;process->deleteLater();
      }
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),dialog,[this,dialog,job,form,process,canAsk,weakRunner,operation=request["op"].toString(),threads,newChat,older,scope](int exitCode,QProcess::ExitStatus status) {
      applicationEvent("document_finished","",{{"job_id",job->id},{"exit_code",exitCode},{"exit_status",int(status)},{"completed",job->completed},{"duration_ms",job->elapsed.elapsed()}});
      if(!job->prepared.empty() && !job->cancelled) {
        const auto prepared=job->prepared;job->prepared={};job->process=nullptr;process->deleteLater();
        QPointer<QDialog> guard(dialog);
        inference.acquire(backend.acquisition.active(),[this,guard,job,weakRunner,prepared,form](QString endpoint,QString token,QString reason){
          if(!guard || job->cancelled){inference.release();return;}
          if(auto next=weakRunner.lock())(*next)({{"op","explain"},{"question",prepared["question"]},{"prepared",prepared},{"local_model_url",endpoint},{"model_token",token},{"generation_note",reason}});
        });
        return;
      }
      if(operation=="explain")inference.release();
      if(!job->completed) form.status->setText(status==QProcess::CrashExit ? "Document job cancelled or stopped unexpectedly." : QString("Document worker ended without a result (exit %1)").arg(exitCode));
      if(!job->completed && !job->pendingQuestion.isEmpty()) {form.answer->append("No answer saved: job cancelled or stopped.");job->pendingQuestion.clear();}
      threads->setEnabled(true);newChat->setEnabled(true);older->setEnabled(true);scope->setEnabled(true);form.answerSource->setEnabled(true);
      form.importButton->setEnabled(!form.pdfPath->text().isEmpty());form.askButton->setEnabled(canAsk());form.cancelButton->setEnabled(!job->deferred.isEmpty());form.browseButton->setEnabled(true);form.documentSelector->setEnabled(true);form.question->setEnabled(true);form.activateButton->setEnabled(!job->profile.empty()&&!dialog->property("documentArchived").toBool());form.chatOpenSourceButton->setEnabled(QFileInfo::exists(job->citationPdf));form.openPageButton->setEnabled(!job->hash.isEmpty());dialog->setProperty("jobRunning",false);
      form.progress->setRange(0,100);form.progress->setValue(job->completed && exitCode==0 ? 100 : 0);
      form.applyAction->setEnabled(!job->proposal.empty() && job->completed && exitCode==0);job->process=nullptr;process->deleteLater();
    });
    connect(process,&QProcess::started,dialog,[process,request,job] {
      applicationEvent("document_process_started",process->program(),{{"job_id",job->id},{"pid",qint64(process->processId())},{"operation",request["op"]}});
      process->write(QJsonDocument(request).toJson(QJsonDocument::Compact)+"\n");process->closeWriteChannel();
    });
    auto timeout=new QTimer(process);timeout->setSingleShot(true);
    connect(timeout,&QTimer::timeout,process,[process,form] {form.status->setText("Document job exceeded 15 minutes and was stopped.");process->kill();});
    timeout->start(15*60*1000);
    process->start(python,{"-u",script});
  };
  const auto run=[runner](QJsonObject request){(*runner)(std::move(request));};
  form.answer->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(form.answer,&QTextBrowser::customContextMenuRequested,dialog,[form,job,run](const QPoint &point) {
    const auto href=form.answer->anchorAt(point);
    if(!href.startsWith("turn://"))return;
    bool ok=false;const int index=href.mid(7).toInt(&ok);
    if(!ok||index<0||index>=job->history.size())return;
    const auto turn=job->history[index].toObject();
    const auto messageId=turn["message_id"].toVariant().toLongLong();
    if(messageId<=0)return;
    QMenu menu(form.answer);
    auto copy=menu.addAction("Copy message");
    auto archive=menu.addAction("Archive message");
    auto remove=menu.addAction("Delete message…");
    const auto selected=menu.exec(form.answer->viewport()->mapToGlobal(point));
    if(selected==copy)QApplication::clipboard()->setText(turn["content"].toString());
    else if(selected==archive)run({{"op","conversation_message_archive"},{"conversation_id",job->conversation},{"message_id",messageId}});
    else if(selected==remove && QMessageBox::question(form.answer,"Delete message","Delete this message from the saved conversation?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes)
      run({{"op","conversation_message_delete"},{"conversation_id",job->conversation},{"message_id",messageId}});
  });
  connect(proposeFromChat,&QPushButton::clicked,dialog,[run,form,manage]{
    const auto instruction=form.question->text().trimmed();
    if(instruction.isEmpty()){form.status->setText("Enter a mapping instruction, such as Set TEMP scale to 0.02.");return;}
    manage->setChecked(true);
    form.workspaceTabs->setCurrentIndex(2);
    form.actionQuestion->setText(instruction);
    run({{"op","propose"},{"question",instruction}});
  });
  auto deferredTimer=new QTimer(dialog);deferredTimer->setInterval(350);
  connect(deferredTimer,&QTimer::timeout,dialog,[this,dialog,job,run,deferAnswer,proposeFromChat,form,legacyFixture,answerMode]{
    const bool recording=backend.acquisition.active()||(connecting&&connectionIsRecording)||savingSession;
    deferAnswer->setVisible(recording&&answerMode->currentIndex()==1&&!job->hash.isEmpty());
    deferAnswer->setEnabled(!dialog->property("jobRunning").toBool());
    proposeFromChat->setEnabled(!recording&&!dialog->property("jobRunning").toBool()&&!job->hash.isEmpty()&&!dialog->property("assistantSourceDeleted").toBool());
    if(!recording&&!job->deferred.isEmpty()&&!dialog->property("jobRunning").toBool()) {
      const auto request=job->deferred.takeFirst();
      dialog->setProperty("queuedIcdJobs",job->deferred.size());
      form.cancelButton->setEnabled(!job->deferred.isEmpty());
      run(request);
    }
  });deferredTimer->start();
  connect(deferAnswer,&QPushButton::clicked,dialog,[run,form]{
    run({{"op","ask"},{"question",form.question->text()},{"defer_generation",true},
      {"answer_source",form.answerSource->currentIndex()==1?"published":"documents"}});
  });
  connect(form.browseButton,&QPushButton::clicked,dialog,[this,dialog,form,job] {
    const auto files=QFileDialog::getOpenFileNames(dialog,"Select ICD PDFs",sessionDir,"PDF (*.pdf)");
    if(files.isEmpty())return;
    QString name;
    QStringList ordered=files;
    if(files.size()>1) {
      QDialog setDialog(dialog);
      setDialog.setWindowTitle("Name and order ICD set");
      auto layout=new QVBoxLayout(&setDialog);
      auto setName=new QLineEdit(&setDialog);
      setName->setMaxLength(120);
      setName->setAccessibleName("ICD set name");
      setName->setPlaceholderText("ICD set name");
      layout->addWidget(setName);
      layout->addWidget(new QLabel("Drag files into document order. Use the buttons to move a selected file.",&setDialog));
      auto list=new QListWidget(&setDialog);
      list->setAccessibleName("PDF import order");
      list->setDragDropMode(QAbstractItemView::InternalMove);
      list->addItems(files);
      layout->addWidget(list);
      auto moveButtons=new QHBoxLayout;
      layout->addLayout(moveButtons);
      for(const int step:{-1,1}) {
        auto button=new QPushButton(step<0?"Move up":"Move down",&setDialog);
        moveButtons->addWidget(button);
        connect(button,&QPushButton::clicked,&setDialog,[list,step]{
          const int row=list->currentRow(), target=row+step;
          if(row<0 || target<0 || target>=list->count())return;
          list->insertItem(target,list->takeItem(row));
          list->setCurrentRow(target);
        });
      }
      auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&setDialog);
      layout->addWidget(buttons);
      buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
      connect(setName,&QLineEdit::textChanged,&setDialog,[buttons](const QString &text){
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
      });
      connect(buttons,&QDialogButtonBox::accepted,&setDialog,&QDialog::accept);
      connect(buttons,&QDialogButtonBox::rejected,&setDialog,&QDialog::reject);
      setDialog.resize(680,360);
      if(setDialog.exec()!=QDialog::Accepted)return;
      name=setName->text().trimmed();
      ordered.clear();
      for(int i=0;i<list->count();++i)ordered.append(list->item(i)->text());
    }
    job->files=ordered;
    job->setName=name;
    job->fileLabel=name.isEmpty()?ordered.front():QString("%1 · %2 PDFs").arg(name).arg(ordered.size());
    form.pdfPath->setText(job->fileLabel);
  });
  connect(form.pdfPath,&QLineEdit::textEdited,dialog,[job]{job->files.clear();job->setName.clear();});
  connect(form.importButton,&QPushButton::clicked,dialog,[run,form,job] {
    if(form.pdfPath->text().isEmpty())return;
    QJsonObject request{{"op","ingest"},{"pdf",form.pdfPath->text()}};
    if(!job->files.isEmpty() && form.pdfPath->text()==job->fileLabel) {
      request["pdfs"]=QJsonArray::fromStringList(job->files);
      request["set_name"]=job->setName;
    }
    run(request);
  });
  connect(form.pdfPath,&QLineEdit::textChanged,dialog,[form,dialog](const QString &path){form.importButton->setEnabled(!path.isEmpty() && !dialog->property("jobRunning").toBool());});
  connect(newChat,&QPushButton::clicked,dialog,[run]{run({{"op","conversation_new"}});});
  connect(threads,&QListWidget::itemClicked,dialog,[run](QListWidgetItem *item){run({{"op","conversation_load"},{"conversation_id",item->data(Qt::UserRole).toJsonObject()["id"]}});});
  connect(search,&QLineEdit::textChanged,dialog,[threads](const QString &text){for(int i=0;i<threads->count();++i)threads->item(i)->setHidden(!threads->item(i)->text().contains(text,Qt::CaseInsensitive));});
  connect(older,&QPushButton::clicked,dialog,[run,job]{if(job->history.isEmpty()||job->conversation.isEmpty())return;run({{"op","conversation_load"},{"conversation_id",job->conversation},{"before",job->history.first().toObject()["message_id"]}});});
  connect(form.documentSelector,&QComboBox::currentIndexChanged,dialog,[run,form,job,canAsk](int){const auto hash=form.documentSelector->currentData().toString();if(!hash.isEmpty()){job->conversation.clear();run({{"op","library"},{"document_hash",hash}});}else {job->hash.clear();job->conversation.clear();job->profile={};job->history={};job->citationPdf.clear();form.answer->clear();form.mappingTable->setRowCount(0);form.askButton->setEnabled(canAsk());form.activateButton->setEnabled(false);form.chatOpenSourceButton->setEnabled(false);form.openPageButton->setEnabled(false);form.scopeLabel->setText("Choose an indexed ICD");}});
  connect(form.chatOpenSourceButton,&QPushButton::clicked,dialog,[job]{if(QFileInfo::exists(job->citationPdf)) {auto url=QUrl::fromLocalFile(job->citationPdf);url.setFragment("page="+QString::number(job->citationPage));QDesktopServices::openUrl(url);}});
  connect(form.answerSource,&QComboBox::currentIndexChanged,dialog,[dialog,form,canAsk] {
    form.askButton->setEnabled(!dialog->property("jobRunning").toBool() && canAsk());
  });
  connect(form.askButton,&QPushButton::clicked,dialog,[run,form,job,answerMode] {
    run({{"op","ask"},{"question",form.question->text()},{"defer_generation",answerMode->currentIndex()==1},
      {"answer_source",form.answerSource->currentIndex()==1?"published":"documents"}});
    if(job->process || !job->deferred.isEmpty()) form.question->clear();
  });
  connect(form.question,&QLineEdit::returnPressed,form.askButton,&QPushButton::click);
  connect(form.previewAction,&QPushButton::clicked,dialog,[run,form]{run({{"op","propose"},{"question",form.actionQuestion->text()}});});
  connect(form.actionQuestion,&QLineEdit::textChanged,dialog,[form,job]{job->proposal={};form.applyAction->setEnabled(false);form.actionPreview->clear();});
  connect(form.applyAction,&QPushButton::clicked,dialog,[this,form,job]{
    if(job->process || job->proposal.empty())return;
    if(!job->draftPath.isEmpty()){reviewMappingDraft(job->draftPath);return;}
    const auto error=applyReviewedAction(job->proposal,job->base,job->hash);form.status->setText(error.isEmpty()?"Plot opened":error);job->proposal={};form.applyAction->setEnabled(false);
  });
  connect(form.cancelButton,&QPushButton::clicked,dialog,[this,job,form,dialog,canAsk,threads,newChat,older,scope] {threads->setEnabled(true);newChat->setEnabled(true);older->setEnabled(true);scope->setEnabled(true);form.answerSource->setEnabled(true);job->cancelled=true;job->prepared={};job->deferred.clear();job->pendingQuestion.clear();if(job->process)job->process->kill();inference.cancel();if(!job->process){dialog->setProperty("jobRunning",false);form.askButton->setEnabled(canAsk());form.question->setEnabled(true);form.documentSelector->setEnabled(true);form.browseButton->setEnabled(true);form.importButton->setEnabled(true);form.cancelButton->setEnabled(false);form.progress->setRange(0,100);form.progress->setValue(0);form.status->setText("Current and queued ICD work cancelled.");}});
  connect(form.activateButton,&QPushButton::clicked,dialog,[this,job,form,legacyFixture] {
    if(!legacyFixture)return;
    QJsonArray accepted;
    for(int row=0;row<form.mappingTable->rowCount();++row) {
      const auto item=form.mappingTable->item(row,0);
      if(item && item->checkState()==Qt::Checked && item->data(Qt::UserRole).isValid()) accepted.append(QJsonObject::fromVariantMap(item->data(Qt::UserRole).toMap()));
    }
    reviewDocument(job->profile,job->original,accepted);
  });
  connect(form.explainEvidence,&QPushButton::clicked,dialog,[form,job,run] {
    if(job->hash.isEmpty() || job->process)return;
    const auto row=form.mappingTable->currentRow();
    if(row<0){form.status->setText("Select a mapping or draft row first.");return;}
    const auto id=form.mappingTable->item(row,0)->text();
    const auto evidence=form.mappingTable->item(row,3)->text();
    const auto range=QRegularExpression(R"((?:^|·\s*)(\d+)\s*(?:[-–—]\s*\d+)?\s*\|)").match(evidence);
    QString question;
    if(!id.isEmpty())question="Describe the conversion for "+id;
    else if(range.hasMatch())question="What does data word "+range.captured(1)+" describe?";
    else question="What message layout is documented on page "+form.mappingTable->item(row,1)->text()+"?";
    form.workspaceTabs->setCurrentIndex(0);
    run({{"op","ask"},{"question",question},{"local_model_url",form.modelUrl->text()},{"model",form.modelName->text()}});
  });
  connect(form.openPageButton,&QPushButton::clicked,dialog,[job,form,sourcePdf] {
    if(job->hash.isEmpty() || job->original.isEmpty()) return;
    const auto row=form.mappingTable->currentRow();const auto cell=row<0 ? nullptr : form.mappingTable->item(row,1);
    const auto path=sourcePdf(job->original,cell?cell->data(Qt::UserRole).toString():QString());
    if(!QFileInfo::exists(path))return;
    auto url=QUrl::fromLocalFile(path);if(cell) url.setFragment("page="+cell->text());QDesktopServices::openUrl(url);
  });
  connect(dialog,&QDialog::finished,dialog,[job,dialog] {if(auto process=job->process.data()) {
    // Widgets may be destroyed before QProcess during parent teardown. Stop
    // callbacks into captured form pointers before cancellation or deletion.
    QObject::disconnect(process,nullptr,dialog,nullptr);process->kill();process->waitForFinished(1000);job->process=nullptr;
  }});
  connect(dialog,&QObject::destroyed,this,[this,job] {inference.cancel();if(auto process=job->process.data()){process->disconnect();process->kill();job->process=nullptr;}});

  observeUiActions(dialog);
  auto reviewImported=new QPushButton("Review imported mappings…",dialog);reviewImported->setIcon(QIcon(":/icons/edit.svg"));form.dialogLayout->insertWidget(6,reviewImported);reviewImported->setVisible(manage->isChecked());connect(manage,&QToolButton::toggled,reviewImported,&QWidget::setVisible);connect(reviewImported,&QPushButton::clicked,form.activateButton,&QPushButton::click);
  if(!legacyFixture) {
    disconnect(manage,&QToolButton::toggled,reviewImported,&QWidget::setVisible);
    reviewImported->hide();
  }
  auto modelState=new QTimer(dialog);modelState->setInterval(1000);connect(modelState,&QTimer::timeout,dialog,[this,form,job,reviewImported]{reviewImported->setEnabled(form.activateButton->isEnabled());if(!job->prepared.empty() || (!job->process && !job->pendingQuestion.isEmpty()))form.status->setText(inference.status());});modelState->start();
  workspaceStack->addWidget(dialog);
  showAssistant();
  if(!pdf.isEmpty()){manage->setChecked(true);} if(!pdf.isEmpty()) QTimer::singleShot(0,form.importButton,&QPushButton::click);
  else QTimer::singleShot(0,dialog,[run,this]{run({{"op","library"},{"preferred_hash",workspaceDocument["icd_revision"].toString(settings.value("icdChatDocument").toString())}});});
}


void Window::reviewMappingDrafts() {
  using DraftList=QList<QPair<QString,QString>>;
  auto watcher=new QFutureWatcher<DraftList>(this);
  connect(watcher,&QFutureWatcher<DraftList>::finished,this,[this,watcher]{
    const auto drafts=watcher->result();watcher->deleteLater();
    if(drafts.isEmpty()){toast("There are no pending ICD Assistant drafts.");return;}
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("ICD Assistant drafts");dialog->resize(630,360);
    auto layout=new QVBoxLayout(dialog);
    auto list=new QListWidget(dialog);list->setAccessibleName("Drafts by subsystem and word");layout->addWidget(list);
    for(const auto &[label,path]:drafts){auto item=new QListWidgetItem(label,list);item->setData(Qt::UserRole,path);}
    list->setCurrentRow(0);
    auto review=new QPushButton("Review selected draft…",dialog);layout->addWidget(review);
    connect(review,&QPushButton::clicked,dialog,[this,dialog,list]{
      if(!list->currentItem())return;
      const auto path=list->currentItem()->data(Qt::UserRole).toString();
      dialog->close();reviewMappingDraft(path);
    });
    dialog->show();
  });
  const auto scope=workspaceDocument["icd_revision"].toString();
  watcher->setFuture(QtConcurrent::run([scope]{
    DraftList drafts;
    const auto files=QDir(mappingDraftDirectory()).entryInfoList({"*.json"},QDir::Files,QDir::Time);
    for(const auto &info:files) {
      QFile file(info.absoluteFilePath());
      if(info.size()>1024*1024||!file.open(QIODevice::ReadOnly))continue;
      const auto draft=QJsonDocument::fromJson(file.readAll()).object();
      if(draft["published"].toBool()||(!scope.isEmpty()&&draft["document_hash"].toString()!=scope))continue;
      const auto proposal=draft["proposal"].toObject();
      if(proposal["kind"]!="mapping")continue;
      for(const auto &row:proposal["profile"].toObject()["signals"].toArray()) {
        const auto signal=row.toObject();
        if(signal["id"]!=proposal["signal"])continue;
        const auto kind=signal["source_word"].toString("payload");
        const auto word=kind=="payload"?QString("Payload %1").arg(signal["word"].toInt()+1):kind;
        drafts.append({signal["subsystem"].toString("Unassigned")+" · "+signal["id"].toString()+" · "+word,info.absoluteFilePath()});
        break;
      }
      if(drafts.size()==100)break;
    }
    return drafts;
  }));
}
void Window::reviewMappingDraft(const QString &path) {
  auto file=QFile(path);
  if(QFileInfo(path).canonicalPath()!=QDir(mappingDraftDirectory()).canonicalPath() || !file.open(QIODevice::ReadOnly) || file.size()>1024*1024){
    toast("Choose a saved ICD Library draft.");return;
  }
  const auto draft=QJsonDocument::fromJson(file.readAll()).object();
  file.close();
  const auto proposal=draft["proposal"].toObject(),base=draft["before"].toObject();
  if(draft["published"].toBool()){toast("This mapping draft was already published.");return;}
  if(desktop::profileToJson(backend.mappings())!=base){toast("The published profile changed. Propose the edit against the current version.");return;}
  std::vector<aero::Signal> mappings;
  if(proposal["kind"]!="mapping"||!desktop::profileFromJson(proposal["profile"].toObject(),mappings).isEmpty()){
    toast("The draft does not contain a valid mapping.");return;
  }
  const auto selected=std::find_if(mappings.begin(),mappings.end(),[&](const auto &mapping){return mapping.id==proposal["signal"].toString().toStdString();});
  if(selected==mappings.end()){toast("The draft signal is missing.");return;}
  const auto signal=*selected;
  QDialog dialog(this);dialog.setObjectName("reviewAssistantDraft");dialog.setWindowTitle("Review ICD mapping draft");dialog.resize(550,390);
  auto layout=new QVBoxLayout(&dialog);
  auto description=new QPlainTextEdit(proposal["summary"].toString()+"\n\nOperator instruction\n"+proposal["instruction"].toString(),&dialog);
  description->setAccessibleName("Mapping change and operator instruction");description->setReadOnly(true);layout->addWidget(description,1);
  if(signal.sourcePage && !signal.sourceHash.empty()){
    const auto source=QString::fromStdString(signal.sourceHash);
    if(QRegularExpression("^[0-9a-f]{64}$").match(source).hasMatch()){
      auto open=new QPushButton(QString("Open cited ICD · page %1").arg(signal.sourcePage),&dialog);layout->addWidget(open);
      connect(open,&QPushButton::clicked,&dialog,[source,signal]{auto url=QUrl::fromLocalFile(documentStore()+"/"+source+".pdf");url.setFragment("page="+QString::number(signal.sourcePage));QDesktopServices::openUrl(url);});
    }
  }
  auto input=new QLineEdit(&dialog);input->setAccessibleName("Example words in hexadecimal");
  input->setObjectName("assistantDraftWords");
  input->setPlaceholderText("Enter example 16-bit words in hexadecimal");layout->addWidget(input);
  auto preview=new QLabel(&dialog);preview->setWordWrap(true);layout->addWidget(preview);
  auto buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
  auto publish=buttons->addButton("Publish mapping",QDialogButtonBox::AcceptRole);layout->addWidget(buttons);
  publish->setObjectName("publishAssistantDraft");
  const auto update=[this,signal,input,preview,publish]{
    std::vector<std::uint16_t> words;
    const auto parts=input->text().split(QRegularExpression("[ ,]+"),Qt::SkipEmptyParts);
    for(const auto &part:parts){bool ok=false;const auto value=part.toUInt(&ok,16);if(!ok || value>65535){preview->setText("Enter 16-bit hexadecimal words.");publish->setEnabled(false);return;}words.push_back(value);}
    const auto record=aero::previewSignalRecord(signal,words,signal.channel?signal.channel:1,signal.bus<0?0:signal.bus);
    const auto result=record.id?aero::decode(record,signal,true):std::nullopt;
    const bool recording=backend.acquisition.active()||connecting||savingSession;
    publish->setEnabled(result.has_value()&&!recording);
    preview->setText(result?QString("Native preview · %1 %2").arg(QString::number(*result,'g',15),QString::fromStdString(signal.unit))+
      (recording?"\nPublish after recording and saving finish.":""):
      QString("Enter %1 word(s) matching this route and layout.").arg((signal.lsb+signal.width+15)/16));
  };
  connect(input,&QLineEdit::textChanged,&dialog,update);
  bool recording=backend.acquisition.active()||connecting||savingSession;
  connect(&tick,&QTimer::timeout,&dialog,[this,update,&recording]{
    const bool busy=backend.acquisition.active()||connecting||savingSession;
    if(busy!=recording){recording=busy;update();}
  });
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  connect(publish,&QPushButton::clicked,&dialog,[&]{
    const auto error=applyReviewedAction(proposal,base,draft["document_hash"].toString());
    if(!error.isEmpty()){preview->setText(error);return;}
    auto published=draft;published["published"]=true;
    if(!writeMappingDraft(path,published))toast("Mapping published. Could not mark its draft as published.");
    else toast("Mapping published for the next recording.");
    dialog.accept();
  });
  update();dialog.exec();
}

QString Window::applyReviewedAction(const QJsonObject &proposal,const QJsonObject &base,const QString &hash) {
  if(desktop::profileToJson(backend.mappings())!=base)return "Active mappings changed; generate a new preview";
  const auto id=proposal["signal"].toString();const auto mappings=backend.mappings();const auto found=std::find_if(mappings.begin(),mappings.end(),[&](const auto &s){return QString::fromStdString(s.id)==id;});
  if(found==mappings.end() || hash.isEmpty() || (QString::fromStdString(found->sourceHash)!=hash &&
    QString::fromStdString(found->icdRevision)!=hash))return "The proposal does not match the active ICD";
  const auto kind=proposal["kind"].toString();std::vector<aero::Signal> updated;
  if(kind=="mapping") {
    if(backend.replaySource)return "Replay uses its recorded profile. Return to the live workspace before changing mappings.";
    if(connecting || backend.acquisition.active())return "Stop capture before changing conversion rules";
    if(auto error=desktop::profileFromJson(proposal["profile"].toObject(),updated);!error.isEmpty())return error;
    if(updated.size()!=mappings.size())return "A clarification must preserve the selected profile";
    const QStringList allowed={"scale","offset","word","lsb","width","rt","sa","channel","bus","signed","transmit","numeric_type","lsw_first","unit","operator_note","source_word"};int changedFields=0;
    for(std::size_t i=0;i<updated.size();++i) {
      const auto before=desktop::signalToJson(mappings[i]),after=desktop::signalToJson(updated[i]);
      if(mappings[i].id!=id.toStdString()){if(before!=after)return "Proposal changed an unrelated signal";continue;}
      for(const auto &key:after.keys())if(before[key]!=after[key]){if(!allowed.contains(key))return "Proposal changed signal identity or provenance";if(key!="operator_note" && !(key=="word"&&before["source_word"]!=after["source_word"]&&after["source_word"]!="payload"&&after["word"].toInt()==0))++changedFields;}
    }
    if(changedFields>1)return "Preview one conversion field at a time";
    if(auto error=QString::fromStdString(aero::validateChecks(backend.captureConfig.checkRules,updated));!error.isEmpty())return error;
  } else if(kind!="plot") return "Unsupported reviewed action";
  const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ReviewedActions";if(!QDir().mkpath(directory))return "Cannot create action audit folder";
  const QJsonObject audit{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"document_hash",hash},{"before",base},{"proposal",proposal}};
  QSaveFile auditFile(directory+"/"+QUuid::createUuid().toString(QUuid::Id128)+".json");
  const auto auditBytes=QJsonDocument(audit).toJson();
  if(!auditFile.open(QIODevice::WriteOnly) || auditFile.write(auditBytes)!=auditBytes.size() || !auditFile.commit())
    return "Cannot save reviewed action audit.";
  if(kind=="mapping") {if(auto error=publishProfile(updated);!error.isEmpty())return error;}
  preferredSignalIndex=unsigned(found-mappings.begin());plotPanelIds[0]=QString::fromStdString(found->id);plotRawColumn=-1;
  analysisChannel=found->channel?found->channel:1;analysisBus=found->bus>=0?found->bus:0;
  if(kind=="plot")navigate(7);else refreshDataViews();
  applicationEvent("reviewed_action_applied",kind+" "+id);return {};
}
