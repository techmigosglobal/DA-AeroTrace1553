#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "services/ui_observer.hpp"
#include "services/document_job.hpp"
#include "ui_document_dialog.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString documentStore() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Documents";
}
QString profilePath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/active-profile.json";
}
QString conversationHtml(const QJsonArray &history,int tableTurn=-1,int tablePage=0) {
  QString html="<style>body{font-family:Segoe UI;color:#25372f} .card{margin:12px;padding:12px} th{background:#edf4f0} td,th{padding:6px;text-align:left} a{color:#287355}</style>";
  for(int i=0;i<history.size();++i) {
    const auto turn=history[i].toObject(), result=turn["result"].toObject();
    const bool user=turn["role"].toString()=="user";
    html+=QString("<a name='turn-%1'></a>").arg(i)+"<div class='card'><b>"+QString(user?"You":"AeroTrace Assistant")+"</b><p>"+turn["content"].toString().toHtmlEscaped().replace("\n","<br>")+"</p>";
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
    int citationNumber=0;for(const auto &item:result["citations"].toArray()){++citationNumber;const auto citation=item.toObject();if(citation["kind"].toString()=="engineering-reference" || citation["kind"].toString()=="native-preview"){html+="<p><b>"+citation["document"].toString().toHtmlEscaped()+"</b> · local reference</p>";continue;}html+=QString("<p><a href='icd://%1/%2'>[%3] %4 · page %2</a></p>").arg(citation["hash"].toString()).arg(citation["page"].toInt()).arg(citationNumber).arg(citation["document"].toString().toHtmlEscaped());}
    html+="</div><hr>";
  }
  return html;
}
}
void Window::persistProfile() {
  QDir().mkpath(QFileInfo(profilePath()).absolutePath());
  QSaveFile output(profilePath());
  if(output.open(QIODevice::WriteOnly)) {
    output.write(QJsonDocument(desktop::profileToJson(backend.profile)).toJson());
    if(!output.commit()) toast("Could not persist active ICD profile");
  } else toast("Could not persist active ICD profile");
}
QString Window::publishProfile(const std::vector<aero::Signal> &mappings) {
  if(property("icdArchivePending").toBool())return "An ICD archive or restore is still completing. Wait before publishing another profile.";
  if(auto error=aero::validateProfile(mappings);!error.empty())return QString::fromStdString(error);
  if(auto error=aero::validateChecks(backend.captureConfig.checkRules,mappings);!error.empty())return QString::fromStdString(error);
  const auto path=profilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile output(path);
  const auto bytes=QJsonDocument(desktop::profileToJson(mappings)).toJson();
  if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())
    return "Could not save active ICD profile: "+output.errorString();
  const auto previous=desktop::profileToJson(backend.profile);
  if(auto error=backend.applyProfile(mappings);!error.empty()) {
    QSaveFile restore(path);
    const auto old=QJsonDocument(previous).toJson();
    if(!restore.open(QIODevice::WriteOnly) || restore.write(old)!=old.size() || !restore.commit())
      return "Publication failed and the previous profile could not be restored on disk: "+QString::fromStdString(error);
    return QString::fromStdString(error);
  }
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
void Window::documentCenter(QString pdf) {
  if(documentDialog) {
    if(documentDock){documentDock->show();documentDock->raise();} documentDialog->show();documentDialog->raise();documentDialog->activateWindow();
    if(!pdf.isEmpty()) {
      if(documentDialog->property("jobRunning").toBool()) {toast("Finish or cancel the current ICD job before opening another PDF.");return;}
      documentDialog->findChild<QLineEdit *>("pdfPath")->setText(pdf);documentDialog->findChild<QPushButton *>("importButton")->click();
    }
    return;
  }
  // Single child process per dialog; Qt only orchestrates file selection and
  // protocol messages. PDF/OCR/model work cannot hold a hardware SDK handle.
  auto dialog=new QDialog(this);
  documentDialog=dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowFlags(Qt::Window);dialog->setWindowTitle("AeroTrace Assistant");dialog->setModal(false);
  Ui::DocumentDialog form;form.setupUi(dialog);
  auto rootLayout=form.dialogLayout;
  auto shell=new QHBoxLayout;rootLayout->addLayout(shell);
  auto historyPanel=new QWidget(dialog);historyPanel->setObjectName("conversationSidebar");historyPanel->setMinimumWidth(170);historyPanel->setMaximumWidth(250);auto historyLayout=new QVBoxLayout(historyPanel);
  auto newChat=new QPushButton("New chat",historyPanel);newChat->setObjectName("newConversation");historyLayout->addWidget(newChat);
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
  };
  auto job=std::make_shared<Job>();
  const auto sourcePdf=[](const QString &original,const QString &hash) {
    static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    return !original.isEmpty() && digest.match(hash).hasMatch()
      ? QFileInfo(original).dir().filePath(hash+".pdf") : original;
  };
  const auto canAsk=[form,job] {
    return form.answerSource->currentIndex()==1 || !job->hash.isEmpty();
  };
  auto scope=new QComboBox(dialog);scope->setObjectName("assistantScope");scope->addItem(workspaceSubsystem.isEmpty()?"All subsystems":workspaceSubsystem,workspaceSubsystem);if(!workspaceSubsystem.isEmpty())scope->addItem("All subsystems",QString());
  auto tools=new QHBoxLayout;tools->addWidget(scope,1);auto expand=new QPushButton("Expand",dialog);expand->setObjectName("expandAssistant");tools->addWidget(expand);form.dialogLayout->insertLayout(2,tools);
  expand->setText("Maximize");connect(expand,&QPushButton::clicked,dialog,[dialog,expand]{if(dialog->isMaximized()){dialog->showNormal();expand->setText("Maximize");}else{dialog->showMaximized();expand->setText("Restore");}});
  auto composer=new QPlainTextEdit(dialog);composer->setObjectName("chatComposer");composer->setPlaceholderText("Ask about this ICD…  Ctrl+Enter to send");composer->setMaximumHeight(100);composer->setMinimumHeight(68);composer->setAccessibleName("Message AeroTrace Assistant");
  form.question->hide();form.questionLayout->insertWidget(0,composer,1);
  connect(composer,&QPlainTextEdit::textChanged,dialog,[composer,form]{const auto text=composer->toPlainText().left(2000);if(form.question->text()!=text)form.question->setText(text);});
  connect(form.question,&QLineEdit::textChanged,dialog,[composer](const QString &text){if(composer->toPlainText()!=text){const QSignalBlocker block(composer);composer->setPlainText(text);}});
  auto sendShortcut=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_Return),dialog);connect(sendShortcut,&QShortcut::activated,form.askButton,&QPushButton::click);

  form.questionLayout->removeWidget(form.answerSource);tools->insertWidget(0,form.answerSource);
  form.fileLayout->removeWidget(form.cancelButton);form.questionLayout->addWidget(form.cancelButton);form.cancelButton->setText("Cancel");
  form.answerSource->setItemText(0,"PDF evidence");
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
  auto runner=std::make_shared<std::function<void(QJsonObject)>>();
  const std::weak_ptr<std::function<void(QJsonObject)>> weakRunner=runner;
  *runner=[this,dialog,form,job,canAsk,sourcePdf,scope,weakRunner,threads,newChat,older](QJsonObject request) {
    applicationEvent("document_requested",request["op"].toString()+" "+QFileInfo(request["pdf"].toString()).fileName());
    if(job->process) {form.status->setText("One document job is already running. Cancel it before starting another.");return;}
    if(request["op"]=="ask" || request["op"]=="explain"){
      if(request["op"]=="ask" && !job->conversation.isEmpty() && (job->conversationScope!=scope->currentData().toString() || job->conversationSource!=(form.answerSource->currentIndex()==1?"published":"documents"))){job->conversation.clear();job->history={};form.answer->clear();}
      request["conversation_id"]=job->conversation;request["native_preview"]=property("nativeConversionPreview").toString();
    }
    if(request["op"]=="conversation_new"){request["document_hash"]=workspaceDocument["icd_revision"].toString();request["subsystem"]=workspaceSubsystem;request["answer_source"]=form.answerSource->currentIndex()==1?"published":"documents";}
    job->id=QUuid::createUuid().toString(QUuid::Id128);job->elapsed.start();
    applicationEvent("document_job_start",request["op"].toString(),{{"job_id",job->id},{"document_hash",job->hash},{"pdf",request["pdf"]},{"question",request["question"]}});
    job->proposal={};form.actionPreview->clear();form.applyAction->setEnabled(false);
    if(request["op"].toString()=="propose") {
      if(dialog->property("documentArchived").toBool()){form.status->setText("Restore this archived ICD in Manage ICDs before reviewing mapping changes.");return;}
      if(job->hash.isEmpty()){form.status->setText("Select an indexed ICD first");return;}
      job->base=desktop::profileToJson(backend.mappings());request["active_profile"]=job->base;request["document_hash"]=job->hash;
    }
    if(request["op"].toString()=="ask") {
      const bool published=request["answer_source"].toString()=="published";
      if((!published && job->hash.isEmpty()) || request["question"].toString().trimmed().isEmpty()) {form.status->setText("Select an answer source and enter a question.");return;}
      if(published) request["active_profile"]=desktop::profileToJson(backend.mappings());
      request["subsystem"]=scope->currentData().toString();request["prepare_generation"]=true;job->cancelled=false;dialog->setProperty("lastAnswerMode",QString());dialog->setProperty("answerDeltaCount",0);
      request["document_hash"]=job->hash;
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
    form.progress->setRange(0,0);form.status->setText("Processing locally…");
    connect(process,&QProcess::readyReadStandardError,dialog,[process] {applicationEvent("document_stderr",QString::fromUtf8(process->readAllStandardError()));});
    connect(process,&QProcess::readyReadStandardOutput,dialog,[this,dialog,process,job,form,sourcePdf,threads,scope] {
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
          if(message.contains("archived"))dialog->setProperty("documentArchived",message["archived"].toBool());
          if(message.contains("pdf_path")) {job->original=message["pdf_path"].toString();if(message["library"].toBool()) form.pdfPath->clear();}
          if(message.contains("history")) {if(message["append_history"].toBool()){auto previous=message["history"].toArray();for(const auto &turn:job->history)previous.append(turn);job->history=previous;}else job->history=message["history"].toArray();job->pendingQuestion.clear();form.answer->setHtml(conversationHtml(job->history));form.answer->scrollToAnchor(message["append_history"].toBool()?"turn-0":"turn-"+QString::number(job->history.size()-1));}
          const auto citations=message["citations"].toArray();
          const auto citation=citations.isEmpty()?QJsonObject():citations[0].toObject();
          job->citationPage=citation["page"].toInt(1);
          job->citationPdf=citation.isEmpty()?QString():sourcePdf(job->original,citation["hash"].toString());
          form.chatOpenSourceButton->setEnabled(QFileInfo::exists(job->citationPdf));
          if(message.contains("proposal")) {
            job->proposal=message["proposal"].toObject();form.actionPreview->setPlainText(job->proposal["summary"].toString());form.status->setText("Review the proposed action and cited ICD before applying.");
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
              rememberIcdDocument(job->profile,job->original);
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
              auto item=form.mappingTable->item(row,0);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Unchecked);item->setData(Qt::UserRole,mapping.toVariantMap());++row;
            }
            for(const auto &entry:review) {
              const auto item=entry.toObject();const QStringList cells={item["id"].toString(),QString::number(item["page"].toInt()),"Review",item["reason"].toString()+" · "+item["text"].toString()};
              for(int column=0;column<4;++column) form.mappingTable->setItem(row,column,new QTableWidgetItem(cells[column]));
              form.mappingTable->item(row,1)->setData(Qt::UserRole,item["source_hash"].toString());
              form.mappingTable->item(row,1)->setToolTip(item["source_document"].toString());
              if(item["candidate"].isObject()) {auto cell=form.mappingTable->item(row,0);cell->setFlags(cell->flags()|Qt::ItemIsUserCheckable);cell->setCheckState(Qt::Unchecked);cell->setData(Qt::UserRole,item["candidate"].toObject().toVariantMap());}
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
            form.workspaceTabs->setTabText(1,review.empty() ? "Mappings and evidence" : QString("Mappings · %1 review").arg(review.size()));
            form.status->setText(QString("%1 extracted mappings · %2 unresolved rows. %3").arg(mappings.size()).arg(review.size()).arg(imported ? "Open Review signals to stage complete mappings and draft fields. Check specific mappings to stage only those." : "Conversation restored. Activating mappings is separate from chatting."));
          } else if(message.contains("answer")) {
            if(!message.contains("history")) {
              if(!job->pendingQuestion.isEmpty())job->history.append(QJsonObject{{"role","user"},{"content",job->pendingQuestion}});
              job->history.append(QJsonObject{{"role","assistant"},{"content",message["answer"]},{"result",message}});job->pendingQuestion.clear();form.answer->setHtml(conversationHtml(job->history));
            }
            dialog->setProperty("lastAnswerMode",message["mode"]);dialog->setProperty("lastCitationCount",message["citations"].toArray().size());
            const auto mode=message["mode"].toString();
            form.status->setText(mode=="published-mapping" ? "Published mapping used by Engineering." :
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
      form.importButton->setEnabled(!form.pdfPath->text().isEmpty());form.askButton->setEnabled(canAsk());form.cancelButton->setEnabled(false);form.browseButton->setEnabled(true);form.documentSelector->setEnabled(true);form.question->setEnabled(true);form.activateButton->setEnabled(!job->profile.empty()&&!dialog->property("documentArchived").toBool());form.chatOpenSourceButton->setEnabled(QFileInfo::exists(job->citationPdf));form.openPageButton->setEnabled(!job->hash.isEmpty());dialog->setProperty("jobRunning",false);
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
  connect(form.askButton,&QPushButton::clicked,dialog,[run,form,job] {
    run({{"op","ask"},{"question",form.question->text()},{"local_model_url",form.modelUrl->text()},
      {"model",form.modelName->text()},{"answer_source",form.answerSource->currentIndex()==1?"published":"documents"}});
    if(job->process) form.question->clear();
  });
  connect(form.question,&QLineEdit::returnPressed,form.askButton,&QPushButton::click);
  connect(form.previewAction,&QPushButton::clicked,dialog,[run,form]{run({{"op","propose"},{"question",form.actionQuestion->text()}});});
  connect(form.actionQuestion,&QLineEdit::textChanged,dialog,[form,job]{job->proposal={};form.applyAction->setEnabled(false);form.actionPreview->clear();});
  connect(form.applyAction,&QPushButton::clicked,dialog,[this,form,job]{
    if(job->process || job->proposal.empty())return;
    const auto error=applyReviewedAction(job->proposal,job->base,job->hash);form.status->setText(error.isEmpty()?"Reviewed action applied to live conversion and plots":error);job->proposal={};form.applyAction->setEnabled(false);
  });
  connect(form.cancelButton,&QPushButton::clicked,dialog,[this,job,form,dialog,canAsk,threads,newChat,older,scope] {threads->setEnabled(true);newChat->setEnabled(true);older->setEnabled(true);scope->setEnabled(true);form.answerSource->setEnabled(true);job->cancelled=true;job->prepared={};job->pendingQuestion.clear();if(job->process)job->process->kill();inference.cancel();if(!job->process){dialog->setProperty("jobRunning",false);form.askButton->setEnabled(canAsk());form.question->setEnabled(true);form.documentSelector->setEnabled(true);form.browseButton->setEnabled(true);form.importButton->setEnabled(true);form.cancelButton->setEnabled(false);form.progress->setRange(0,100);form.progress->setValue(0);form.status->setText("Cancelled. No answer saved.");}});
  connect(form.activateButton,&QPushButton::clicked,dialog,[this,job,form] {
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
  auto modelState=new QTimer(dialog);modelState->setInterval(1000);connect(modelState,&QTimer::timeout,dialog,[this,form,job,reviewImported]{reviewImported->setEnabled(form.activateButton->isEnabled());if(!job->prepared.empty() || (!job->process && !job->pendingQuestion.isEmpty()))form.status->setText(inference.status());});modelState->start();
  const auto available=screen()->availableGeometry();
  dialog->resize(QStandardPaths::isTestModeEnabled() ? 1120 : std::min(1120,available.width()-40),QStandardPaths::isTestModeEnabled() ? 660 : std::min(820,available.height()-60));dialog->show();
  if(!pdf.isEmpty()){manage->setChecked(true);} if(!pdf.isEmpty()) QTimer::singleShot(0,form.importButton,&QPushButton::click);
  else QTimer::singleShot(0,dialog,[run,this]{run({{"op","library"},{"preferred_hash",workspaceDocument["icd_revision"].toString(settings.value("icdChatDocument").toString())}});});
}


QString Window::applyReviewedAction(const QJsonObject &proposal,const QJsonObject &base,const QString &hash) {
  if(desktop::profileToJson(backend.mappings())!=base)return "Active mappings changed; generate a new preview";
  const auto id=proposal["signal"].toString();const auto mappings=backend.mappings();const auto found=std::find_if(mappings.begin(),mappings.end(),[&](const auto &s){return QString::fromStdString(s.id)==id;});
  if(found==mappings.end() || hash.isEmpty() || QString::fromStdString(found->sourceHash)!=hash)return "The proposal does not match the active ICD";
  const auto kind=proposal["kind"].toString();std::vector<aero::Signal> updated;
  if(kind=="mapping") {
    if(backend.replaySource)return "Replay uses its recorded profile. Return to the live workspace before changing mappings.";
    if(connecting || backend.acquisition.active())return "Stop capture before changing conversion rules";
    if(auto error=desktop::profileFromJson(proposal["profile"].toObject(),updated);!error.isEmpty())return error;
    if(updated.size()!=mappings.size())return "A clarification must preserve the selected profile";
    const QStringList allowed={"scale","offset","word","lsb","width","rt","sa","channel","bus","signed","transmit","numeric_type","lsw_first","unit","operator_note"};int changedFields=0;
    for(std::size_t i=0;i<updated.size();++i) {
      const auto before=desktop::signalToJson(mappings[i]),after=desktop::signalToJson(updated[i]);
      if(mappings[i].id!=id.toStdString()){if(before!=after)return "Proposal changed an unrelated signal";continue;}
      for(const auto &key:after.keys())if(before[key]!=after[key]){if(!allowed.contains(key))return "Proposal changed signal identity or provenance";if(key!="operator_note")++changedFields;}
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
