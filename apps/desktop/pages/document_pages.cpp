#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "services/task_services.hpp"
#include "services/ui_observer.hpp"
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
QString conversationText(const QJsonArray &history) {
  QStringList blocks;
  for(int i=std::max(0,int(history.size())-4);i<history.size();++i) {const auto turn=history[i].toObject();blocks<<(turn["role"].toString()=="user" ? "You" : "ICD Assistant")+QString("\n")+turn["content"].toString();}
  return blocks.join("\n\n────────────────────────\n\n");
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
void Window::activateProfile(const QJsonObject &object) {
  std::vector<aero::Signal> mappings;
  if(auto error=desktop::profileFromJson(object,mappings);!error.isEmpty()) {toast(error);return;}
  if(auto error=backend.applyProfile(std::move(mappings));!error.empty()) {toast(QString::fromStdString(error));return;}
  persistProfile();
  applicationEvent("profile_activated","",{{"signals",int(backend.profile.size())},{"profile_sha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(desktop::profileToJson(backend.profile)).toJson(),QCryptographicHash::Sha256).toHex())}});
  eventText=QString("Activated %1 validated ICD signals").arg(backend.profile.size());
  if(unlocked) navigate(12);
  toast(eventText);
}
void Window::documentCenter(QString pdf) {
  if(documentDialog) {
    documentDialog->show();documentDialog->raise();documentDialog->activateWindow();
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
  Ui::DocumentDialog form;form.setupUi(dialog);
  form.pdfPath->setText(pdf);
  form.modelUrl->setText(settings.value("localModelUrl").toString());
  form.modelName->setText(settings.value("localModelName","local").toString());
  form.cancelButton->setEnabled(false);
  form.askButton->setEnabled(false);form.documentSelector->addItem("Choose an indexed ICD…",QString());
  form.chatOpenSourceButton->setEnabled(false);form.openPageButton->setEnabled(false);form.progress->setTextVisible(false);form.progress->setFormat(QString());form.progress->setMaximumHeight(6);form.dialogLayout->setSpacing(8);
  form.documentSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);form.documentSelector->setMinimumContentsLength(24);
  form.answer->setFont(QApplication::font());form.question->setMaxLength(2000);form.question->setToolTip("Ask: What is TEMP's scale?\nPreview: Set TEMP scale to 0.01; TEMP offset is -40; Plot TEMP. Word indices are zero-based.");
  form.mappingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  form.mappingTable->horizontalHeader()->setStretchLastSection(true);
  struct Job {
    QPointer<QProcess> process;
    QByteArray buffer;
    QJsonObject profile;
    QJsonObject proposal,base;
    QString original;
    QString hash;
    QString pendingQuestion;
    QJsonArray history;
    unsigned citationPage{1};
    QString id;QElapsedTimer elapsed;
    bool completed{};
  };
  auto job=std::make_shared<Job>();
  auto run=[this,dialog,form,job](QJsonObject request) {
    applicationEvent("document_requested",request["op"].toString()+" "+QFileInfo(request["pdf"].toString()).fileName());
    if(job->process) {form.status->setText("One document job is already running. Cancel it before starting another.");return;}
    job->id=QUuid::createUuid().toString(QUuid::Id128);job->elapsed.start();
    applicationEvent("document_job_start",request["op"].toString(),{{"job_id",job->id},{"document_hash",job->hash},{"pdf",request["pdf"]},{"question",request["question"]}});
    job->proposal={};form.actionPreview->clear();form.applyAction->setEnabled(false);
    if(request["op"].toString()=="propose") {
      if(job->hash.isEmpty()){form.status->setText("Select an indexed ICD first");return;}
      job->base=desktop::profileToJson(backend.mappings());request["active_profile"]=job->base;request["document_hash"]=job->hash;
    }
    if(request["op"].toString()=="ask") {
      if(job->hash.isEmpty() || request["question"].toString().trimmed().isEmpty()) {form.status->setText("Choose an ICD and enter a question first.");return;}
      request["document_hash"]=job->hash;
      job->pendingQuestion=request["question"].toString();form.answer->appendPlainText("\nYou\n"+job->pendingQuestion+"\n\nICD Assistant\nSearching this ICD…");
    }
    if(request["op"].toString()=="ingest") {job->hash.clear();job->profile={};form.answer->clear();form.scopeLabel->setText("Indexing the selected PDF…");const QSignalBlocker block(form.documentSelector);form.documentSelector->setCurrentIndex(0);}
    QString python=QCoreApplication::applicationDirPath()+"/document-runtime/python.exe";
    const bool deployed=QFileInfo::exists(QCoreApplication::applicationDirPath()+"/deployment.json");
    if(!QFileInfo::exists(python) && !deployed) python=QDir(qEnvironmentVariable("ProgramFiles","C:/Program Files")).filePath("AeroTrace1553/document-runtime/python.exe");
    if(!QFileInfo::exists(python) && !deployed) python=QString::fromUtf8(AEROTRACE_DOCUMENT_PYTHON);
    QString script=QCoreApplication::applicationDirPath()+"/document-worker/worker.py";
    if(!QFileInfo::exists(script) && !deployed) script=QString::fromUtf8(AEROTRACE_DOCUMENT_WORKER);
    if(!QFileInfo::exists(python) || !QFileInfo::exists(script)) {
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
    environment.insert("PYTHONNOUSERSITE","1");environment.remove("PYTHONHOME");environment.remove("PYTHONPATH");
    environment.insert("OPENBLAS_NUM_THREADS","1");environment.insert("OMP_NUM_THREADS","1");environment.insert("MKL_NUM_THREADS","1");
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    form.importButton->setEnabled(false);form.askButton->setEnabled(false);form.cancelButton->setEnabled(true);form.activateButton->setEnabled(false);
    form.browseButton->setEnabled(false);form.documentSelector->setEnabled(false);form.question->setEnabled(false);form.chatOpenSourceButton->setEnabled(false);form.openPageButton->setEnabled(false);
    form.progress->setRange(0,0);form.status->setText("Processing locally…");
    connect(process,&QProcess::readyReadStandardError,dialog,[process] {applicationEvent("document_stderr",QString::fromUtf8(process->readAllStandardError()));});
    connect(process,&QProcess::readyReadStandardOutput,dialog,[this,dialog,process,job,form] {
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
        } else if(event=="error") {job->completed=true;form.status->setText(message["message"].toString());if(!job->pendingQuestion.isEmpty()) {form.answer->appendPlainText("No answer saved: "+message["message"].toString());job->pendingQuestion.clear();}}
        else if(event=="done") {
          job->completed=true;
          if(message.contains("document_hash")) {job->hash=message["document_hash"].toString();if(!job->hash.isEmpty()) settings.setValue("icdChatDocument",job->hash);}
          if(message.contains("documents")) {
            const QSignalBlocker block(form.documentSelector);form.documentSelector->clear();form.documentSelector->addItem("Choose an indexed ICD…",QString());
            for(const auto &entry:message["documents"].toArray()) {const auto doc=entry.toObject();form.documentSelector->addItem(doc["name"].toString()+" · "+doc["hash"].toString().left(8),doc["hash"].toString());}
            form.documentSelector->setCurrentIndex(std::max(0,form.documentSelector->findData(job->hash)));
          }
          if(message.contains("pdf_path")) {job->original=message["pdf_path"].toString();if(message["library"].toBool()) form.pdfPath->clear();}
          if(message.contains("history")) {job->history=message["history"].toArray();job->pendingQuestion.clear();form.answer->setPlainText(conversationText(job->history));form.answer->moveCursor(QTextCursor::End);}
          const auto citations=message["citations"].toArray();if(!citations.empty()) job->citationPage=citations[0].toObject()["page"].toInt(1);
          if(message.contains("proposal")) {
            job->proposal=message["proposal"].toObject();form.actionPreview->setPlainText(job->proposal["summary"].toString());form.status->setText("Review the proposed action and cited ICD before applying.");
          } else if(message.contains("profile")) {
            job->profile=message["profile"].toObject();job->original=message["pdf_path"].toString();
            const auto mappings=job->profile["signals"].toArray();const auto review=job->profile["review"].toArray();
            form.mappingTable->setRowCount(mappings.size()+review.size());
            int row=0;
            for(const auto &entry:mappings) {
              const auto mapping=entry.toObject();
              const QStringList cells={mapping["id"].toString(),QString::number(mapping["source_page"].toInt()),"Validated",mapping["evidence"].toObject()["text"].toString()};
              for(int column=0;column<4;++column) form.mappingTable->setItem(row,column,new QTableWidgetItem(cells[column]));
              auto item=form.mappingTable->item(row,0);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(Qt::Checked);item->setData(Qt::UserRole,mapping.toVariantMap());++row;
            }
            for(const auto &entry:review) {
              const auto item=entry.toObject();const QStringList cells={item["id"].toString(),QString::number(item["page"].toInt()),"Review",item["reason"].toString()+" · "+item["text"].toString()};
              for(int column=0;column<4;++column) form.mappingTable->setItem(row,column,new QTableWidgetItem(cells[column]));
              if(item["candidate"].isObject()) {auto cell=form.mappingTable->item(row,0);cell->setFlags(cell->flags()|Qt::ItemIsUserCheckable);cell->setCheckState(Qt::Unchecked);cell->setData(Qt::UserRole,item["candidate"].toObject().toVariantMap());}
              ++row;
            }
            form.activateButton->setEnabled(form.mappingTable->rowCount()>0);
            const bool imported=!message["library"].toBool();
            if(imported && job->profile["automatic"].toBool()) activateProfile(job->profile);
            const bool active=!backend.profile.empty() && std::all_of(backend.profile.begin(),backend.profile.end(),[&](const auto &signal){return QString::fromStdString(signal.sourceHash)==job->hash;});
            form.scopeLabel->setText(QString("%1 · %2 pages · %3 · %4").arg(job->profile["document"].toString()).arg(job->profile["pages"].toInt()).arg(job->hash.left(12)).arg(active ? "Active conversion ICD" : "Chat scope only; conversion profile unchanged"));
            form.workspaceTabs->setTabText(1,review.empty() ? "Mappings and evidence" : QString("Mappings · %1 review").arg(review.size()));
            form.status->setText(QString("%1 validated mappings · %2 rows need review. %3").arg(mappings.size()).arg(review.size()).arg(imported && job->profile["automatic"].toBool() ? "Profile activated automatically." : imported ? "Review uncertain rules before activation." : "Conversation restored. Activating mappings is separate from chatting."));
          } else if(message.contains("answer")) {
            if(!message.contains("history")) form.answer->setPlainText(message["answer"].toString());
            form.status->setText(message["mode"].toString()=="local-model" ? "Local model answer; verify its source citations." : message["mode"].toString()=="validated-mapping" ? "Answer from the selected ICD's validated mapping. Conversion rules are unchanged." : "Evidence retrieved only from the selected ICD. Conversion rules are unchanged.");
          } else if(message["library"].toBool()) {
            job->hash.clear();form.mappingTable->setRowCount(0);form.scopeLabel->setText("No indexed ICD selected · conversion profile unchanged");form.status->setText("Import a PDF to start its own conversation.");
          }
        }
      }
    });
    connect(process,&QProcess::errorOccurred,dialog,[dialog,form,job,process](QProcess::ProcessError error) {
      applicationEvent("document_process_error",process->errorString());
      form.status->setText("Could not run document worker. Check the installed document runtime.");
      if(error==QProcess::FailedToStart) {
        form.importButton->setEnabled(true);form.askButton->setEnabled(!job->hash.isEmpty());form.cancelButton->setEnabled(false);form.browseButton->setEnabled(true);form.documentSelector->setEnabled(true);form.question->setEnabled(true);dialog->setProperty("jobRunning",false);
        form.progress->setRange(0,100);form.progress->setValue(0);job->process=nullptr;process->deleteLater();
      }
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),dialog,[dialog,job,form,process](int exitCode,QProcess::ExitStatus status) {
      applicationEvent("document_finished","",{{"job_id",job->id},{"exit_code",exitCode},{"exit_status",int(status)},{"completed",job->completed},{"duration_ms",job->elapsed.elapsed()}});
      if(!job->completed) form.status->setText(status==QProcess::CrashExit ? "Document job cancelled or stopped unexpectedly." : QString("Document worker ended without a result (exit %1)").arg(exitCode));
      if(!job->completed && !job->pendingQuestion.isEmpty()) {form.answer->appendPlainText("No answer saved: job cancelled or stopped.");job->pendingQuestion.clear();}
      form.importButton->setEnabled(!form.pdfPath->text().isEmpty());form.askButton->setEnabled(!job->hash.isEmpty());form.cancelButton->setEnabled(false);form.browseButton->setEnabled(true);form.documentSelector->setEnabled(true);form.question->setEnabled(true);form.activateButton->setEnabled(!job->profile.empty());form.chatOpenSourceButton->setEnabled(!job->hash.isEmpty());form.openPageButton->setEnabled(!job->hash.isEmpty());dialog->setProperty("jobRunning",false);
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
  connect(form.browseButton,&QPushButton::clicked,dialog,[this,dialog,form] {
    const auto path=QFileDialog::getOpenFileName(dialog,"Import ICD document",sessionDir,"PDF (*.pdf)");
    if(!path.isEmpty()) form.pdfPath->setText(path);
  });
  connect(form.importButton,&QPushButton::clicked,dialog,[run,form] {if(!form.pdfPath->text().isEmpty()) run({{"op","ingest"},{"pdf",form.pdfPath->text()}});});
  connect(form.pdfPath,&QLineEdit::textChanged,dialog,[form,dialog](const QString &path){form.importButton->setEnabled(!path.isEmpty() && !dialog->property("jobRunning").toBool());});
  connect(form.documentSelector,&QComboBox::currentIndexChanged,dialog,[run,form,job](int){const auto hash=form.documentSelector->currentData().toString();if(!hash.isEmpty()) run({{"op","library"},{"document_hash",hash}});else {job->hash.clear();job->profile={};form.answer->clear();form.mappingTable->setRowCount(0);form.askButton->setEnabled(false);form.activateButton->setEnabled(false);form.chatOpenSourceButton->setEnabled(false);form.openPageButton->setEnabled(false);form.scopeLabel->setText("Choose an indexed ICD · conversion profile unchanged");}});
  connect(form.chatOpenSourceButton,&QPushButton::clicked,dialog,[job]{if(!job->hash.isEmpty() && !job->original.isEmpty()) {auto url=QUrl::fromLocalFile(job->original);url.setFragment("page="+QString::number(job->citationPage));QDesktopServices::openUrl(url);}});
  connect(form.askButton,&QPushButton::clicked,dialog,[run,form,job] {run({{"op","ask"},{"question",form.question->text()},{"local_model_url",form.modelUrl->text()},{"model",form.modelName->text()}});if(job->process) form.question->clear();});
  connect(form.question,&QLineEdit::returnPressed,form.askButton,&QPushButton::click);
  connect(form.previewAction,&QPushButton::clicked,dialog,[run,form]{run({{"op","propose"},{"question",form.question->text()}});});
  connect(form.question,&QLineEdit::textEdited,dialog,[form,job]{job->proposal={};form.applyAction->setEnabled(false);form.actionPreview->clear();});
  connect(form.applyAction,&QPushButton::clicked,dialog,[this,form,job]{
    if(job->process || job->proposal.empty())return;
    const auto error=applyReviewedAction(job->proposal,job->base,job->hash);form.status->setText(error.isEmpty()?"Reviewed action applied to live conversion and plots":error);job->proposal={};form.applyAction->setEnabled(false);
  });
  connect(form.cancelButton,&QPushButton::clicked,dialog,[job] {if(job->process) job->process->kill();});
  connect(form.activateButton,&QPushButton::clicked,dialog,[this,job,form] {
    QJsonArray accepted;
    for(int row=0;row<form.mappingTable->rowCount();++row) {
      const auto item=form.mappingTable->item(row,0);
      if(item && item->checkState()==Qt::Checked && item->data(Qt::UserRole).isValid()) accepted.append(QJsonObject::fromVariantMap(item->data(Qt::UserRole).toMap()));
    }
    if(accepted.empty()) {form.status->setText("Check validated signals or verify candidate rows against their source PDF before selecting them.");return;}
    auto profile=job->profile;profile["signals"]=accepted;activateProfile(profile);
  });
  connect(form.openPageButton,&QPushButton::clicked,dialog,[job,form] {
    if(job->hash.isEmpty() || job->original.isEmpty()) return;
    const auto row=form.mappingTable->currentRow();const auto cell=row<0 ? nullptr : form.mappingTable->item(row,1);
    auto url=QUrl::fromLocalFile(job->original);if(cell) url.setFragment("page="+cell->text());QDesktopServices::openUrl(url);
  });
  connect(dialog,&QDialog::finished,dialog,[job,dialog] {if(auto process=job->process.data()) {
    // Widgets may be destroyed before QProcess during parent teardown. Stop
    // callbacks into captured form pointers before cancellation or deletion.
    QObject::disconnect(process,nullptr,dialog,nullptr);process->kill();process->waitForFinished(1000);job->process=nullptr;
  }});
  connect(dialog,&QObject::destroyed,this,[job] {if(auto process=job->process.data()){process->disconnect();process->kill();job->process=nullptr;}});
  observeUiActions(dialog);
  const auto available=screen()->availableGeometry();
  dialog->resize(QStandardPaths::isTestModeEnabled() ? 1120 : std::min(1120,available.width()-40),QStandardPaths::isTestModeEnabled() ? 660 : std::min(820,available.height()-60));dialog->show();
  if(!pdf.isEmpty()) QTimer::singleShot(0,form.importButton,&QPushButton::click);
  else QTimer::singleShot(0,dialog,[run,this]{run({{"op","library"},{"preferred_hash",settings.value("icdChatDocument").toString()}});});
}

