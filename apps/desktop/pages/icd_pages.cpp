#include "main_window.hpp"
#include "ui_icd_library_page.h"
#include "ui_icd_editor_page.h"
#include "ui_exports_page.h"
#include "services/profile_io.hpp"

void Window::icdLibrary() {
  auto form=showForm<Ui::IcdLibraryPage>();
  auto manage=new QPushButton("Manage ICDs",content);titleActions->addWidget(manage);connect(manage,&QPushButton::clicked,this,[this]{manageIcds();});
  connect(form.importIcdButton,&QPushButton::clicked,this,[this] { importIcd(); });
  connect(form.documentsButton,&QPushButton::clicked,this,[this] { documentCenter(); });
  form.newSignalButton->setEnabled(!backend.replaySource);
  form.newSignalButton->setText(backend.replaySource?"Recorded mappings (read-only)":"Review subsystem…");form.newSignalButton->setIcon(QIcon(":/icons/edit.svg"));
  connect(form.newSignalButton,&QPushButton::clicked,this,[this] {if(!workspaceDocument.empty() && !backend.replaySource)reviewDocument(workspaceDocument,workspacePdf);else reviewSignals();});
  form.importIcdButton->hide();form.documentsButton->hide();form.editSignalButton->hide();form.profileMetrics->hide();
  form.profilesTitle->setText("Mapping preview");
  form.profileNotice->setText("Review signals → Apply conversion preset → Preview raw words → Publish subsystem");
  connect(form.exportProfileButton,&QPushButton::clicked,this,[this] { saveIcd(false); });
  const auto mappings=backend.mappings();QList<QStringList> rows;
  QJsonArray displayed;
  for(const auto &mapping:mappings)if(inWorkspace(mapping)) {
    auto object=desktop::signalToJson(mapping);object["status"]="Published";displayed.append(object);
  }
  if(!backend.replaySource){
    QJsonArray candidates=workspaceDocument["signals"].toArray();for(const auto &entry:workspaceDocument["review"].toArray())candidates.append(entry);
    for(const auto &entry:candidates){auto row=entry.toObject();const auto id=row["id"].toString(row["draft_cells"].toObject()["id"].toString());const auto group=row["subsystem"].toString("Unassigned");
      if(!workspaceSubsystem.isEmpty() && group!=workspaceSubsystem)continue;
      if(std::any_of(mappings.begin(),mappings.end(),[&](const auto &signal){return QString::fromStdString(signal.id)==id;}))continue;
      const auto cells=row["draft_cells"].toObject();
      row["id"]=id;row["word"]=cells["data_word"].toInt(row["word"].toInt()+1)-1;row["width"]=cells["width"].toInt(row["width"].toInt());row["numeric_type"]=cells["representation"].toString(row["numeric_type"].toString("Unspecified"));row["unit"]=cells["unit"].toString(row["unit"].toString());row["status"]="Draft · review";displayed.append(row);
    }
  }
  for(const auto &entry:displayed){const auto row=entry.toObject();const int first=row["word"].toInt()+1,count=(row["width"].toInt()+15)/16;
    rows<<QStringList{(row["source_signal_id"].toString().isEmpty()?row["id"].toString():row["source_signal_id"].toString()),count>1?QString("%1–%2").arg(first).arg(first+count-1):QString::number(first),row["numeric_type"].toString(),row["unit"].toString(),row["status"].toString()};}
  form.profilesTable->setHorizontalHeaderLabels({"SIGNAL ID","WORDS","ENCODING","UNIT","STATUS"});
  populate(form.profilesTable,rows);
  form.profilesTable->horizontalHeaderItem(1)->setToolTip("One-based data-word numbers. PDF citations retain original numbering.");
  for(int row=0;row<form.profilesTable->rowCount();++row)form.profilesTable->item(row,0)->setToolTip(displayed[row].toObject()["id"].toString());
  form.validated->setText(QString::number(mappings.size()));form.revision->setText(backend.replaySource?"Replay snapshot":"Active profile");
  connect(form.editSignalButton,&QPushButton::clicked,this,[this]{if(!workspaceDocument.empty() && !backend.replaySource)reviewDocument(workspaceDocument,workspacePdf);else reviewSignals();});
  connect(form.profilesTable,&QTableWidget::cellDoubleClicked,this,[this,displayed](int row,int){if(row<0 || row>=displayed.size())return;if(displayed[row].toObject()["status"].toString().startsWith("Draft")){reviewDocument(workspaceDocument,workspacePdf);return;}aero::Signal signal;if(desktop::signalFromJson(displayed[row].toObject(),signal).isEmpty()){backend.signal=signal;navigate(13);}});

}






void Window::editor() {
  auto form=showForm<Ui::IcdEditorPage>();
  signalId=form.signalId; signalUnit=form.signalUnit; rtEdit=form.rtEdit; saEdit=form.saEdit;
  wordEdit=form.wordEdit; widthEdit=form.widthEdit; lsbEdit=form.lsbEdit; directionEdit=form.directionEdit;
  expectedWordCountEdit=form.expectedWordCount;expectedWordCountEdit->setValue(backend.signal.expectedWordCount);
  signedEdit=form.signedEdit; scaleEdit=form.scaleEdit; offsetEdit=form.offsetEdit; rawEdit=form.rawEdit;
  numericTypeEdit=form.numericType;wordOrderEdit=form.wordOrder;editingSignalId=backend.signal.id;
  numericTypeEdit->setCurrentText(QString::fromStdString(backend.signal.numericType));wordOrderEdit->setCurrentIndex(backend.signal.leastSignificantWordFirst ? 1 : 0);
  preview=form.preview; decodedPreview=form.decodedPreview; scalePreview=form.scalePreview; engineeringPreview=form.engineeringPreview;
  signalId->setText(QString::fromStdString(backend.signal.id)); signalUnit->setText(QString::fromStdString(backend.signal.unit));
  rtEdit->setValue(backend.signal.rt); saEdit->setValue(backend.signal.sa); wordEdit->setValue(backend.signal.word);
  widthEdit->setValue(backend.signal.width); lsbEdit->setValue(backend.signal.lsb);
  directionEdit->setCurrentIndex(backend.signal.transmit ? 1 : 0); signedEdit->setCurrentIndex(backend.signal.signedValue ? 1 : 0);
  scaleEdit->setText(QString::number(backend.signal.scale)); offsetEdit->setText(QString::number(backend.signal.offset));
  connect(form.validateButton,&QPushButton::clicked,this,[this] { validateEditor(); });
  connect(form.saveDraftButton,&QPushButton::clicked,this,[this] { if(aero::validate(editorSignal()).empty()) saveIcd(true);else validateEditor(); });
  connect(form.publishButton,&QPushButton::clicked,this,[this] { if(readEditor()) saveIcd(false); });
  populate(form.checksTable,{{"Word present","Synthetic preview"},{"Type conversion","Validated on input"},{"Valid range","Checked on publish"},{"ICD revision","Local JSON"}});
  for(auto input:{signalId,signalUnit,scaleEdit,offsetEdit,rawEdit}) connect(input,&QLineEdit::textChanged,this,[this] { updatePreview(); });
  for(auto input:{wordEdit,widthEdit,lsbEdit,rtEdit,saEdit,expectedWordCountEdit}) connect(input,&QSpinBox::valueChanged,this,[this] { updatePreview(); });
  connect(signedEdit,&QComboBox::currentIndexChanged,this,[this] { updatePreview(); });
  connect(directionEdit,&QComboBox::currentIndexChanged,this,[this] { updatePreview(); });
  connect(numericTypeEdit,&QComboBox::currentIndexChanged,this,[this] { updatePreview(); });
  connect(wordOrderEdit,&QComboBox::currentIndexChanged,this,[this] { updatePreview(); });
  updatePreview();
}


aero::Signal Window::editorSignal(){
    aero::Signal s=backend.signal;
    s.id = signalId->text().trimmed().toStdString();
    s.unit = signalUnit->text().toStdString();
    s.rt = rtEdit->value();
    s.sa = saEdit->value();
    s.word = wordEdit->value();
    s.width = widthEdit->value();
    s.lsb = lsbEdit->value();
    s.expectedWordCount = unsigned(expectedWordCountEdit->value());
    s.signedValue = signedEdit->currentIndex() == 1;
    s.transmit = directionEdit->currentIndex() == 1;
    s.numericType=numericTypeEdit->currentText().toStdString();s.leastSignificantWordFirst=wordOrderEdit->currentIndex()==1;
    bool a, b;
    s.scale = scaleEdit->text().toDouble(&a);
    s.offset = offsetEdit->text().toDouble(&b);
    if (!a || !b)
      s.width = 0;
    return s;
  }

void Window::updatePreview(){
    auto s = editorSignal();
    const auto pieces=rawEdit->text().trimmed().split(QRegularExpression("[ ,;]+"),Qt::SkipEmptyParts);
    const auto required=(s.lsb+s.width+15)/16;
    std::vector<std::uint16_t> words;
    for(const auto &piece:pieces) {bool valid;auto value=piece.toUInt(&valid,16);if(!valid || value>65535) {words.clear();break;}words.push_back(value);}
    if (words.size()!=required || !aero::validate(s).empty()) {
      preview->setText("Invalid mapping / raw word");
      decodedPreview->setText("—");
      scalePreview->setText("—");
      engineeringPreview->setText("—");
      return;
    }
    aero::Record r;
    r.channel=s.channel ? s.channel : 1;r.bus=s.bus<0 ? 0 : s.bus;
    r.words.resize(s.expectedWordCount ? s.expectedWordCount : s.word + words.size());
    r.commandWord = (s.rt << 11) | (s.transmit << 10) | (s.sa << 5) | (r.words.size() & 31);
    std::copy(words.begin(),words.end(),r.words.begin()+s.word);
    auto v = aero::decode(r, s, true);
    preview->setText(rawEdit->text());
    if (v) {
      scalePreview->setText(QString::number(s.scale));
      engineeringPreview->setText(QString::number(*v, 'f', 2));
      auto unscaled = s;
      unscaled.scale = 1;
      unscaled.offset = 0;
      decodedPreview->setText(QString::number(aero::decode(r, unscaled, true).value_or(*v)));
    }
  }

bool Window::readEditor(){
    auto s = editorSignal();
    auto error = aero::validate(s);
    if (!error.empty()) {
      toast(QString::fromStdString(error));
      return false;
    }
    auto mappings=backend.profile;
    auto found=std::find_if(mappings.begin(),mappings.end(),[this](const auto &mapping) {return mapping.id==editingSignalId;});
    if(found==mappings.end()) mappings.push_back(s);else *found=s;
    if(auto message=publishProfile(mappings);!message.isEmpty()) {toast(message);return false;}
    backend.signal = s;editingSignalId=s.id;
    return true;
  }

void Window::validateEditor(){
    const auto error=aero::validate(editorSignal());
    toast(error.empty() ? "Mapping valid. Publish to apply it to live conversion." : QString::fromStdString(error));
  }

QJsonObject Window::signalJson(){ return desktop::signalToJson(backend.signal); }

void Window::saveIcd(bool draft){
    auto p = QFileDialog::getSaveFileName(
        this, draft ? "Save ICD draft" : "Publish ICD revision",
        sessionDir + "/" + QString::fromStdString(backend.signal.id) + ".json",
        "JSON (*.json)");
    if (p.isEmpty())
      return;
    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
      toast(f.errorString());
      return;
    }
    auto obj = desktop::profileToJson(draft ? std::vector<aero::Signal>{editorSignal()} : backend.profile);
    obj["draft"] = draft;
    f.write(QJsonDocument(obj).toJson());
    toast(f.commit() ? "ICD profile saved" : "ICD save failed");
  }

void Window::importIcd() {
  const auto path=QFileDialog::getOpenFileName(this,"Import ICD",sessionDir,"ICD files (*.pdf *.json *.csv)");
  importIcdPath(path);
}
void Window::importIcdPath(const QString &path) {
  if(path.isEmpty()) return;
  if(QFileInfo(path).suffix().compare("pdf",Qt::CaseInsensitive)==0) {documentCenter(path);return;}
  QFile input(path);
  if(!input.open(QIODevice::ReadOnly) || input.size()>4*1024*1024) {toast("Cannot read ICD profile (limit 4 MB)");return;}
  if(QFileInfo(path).suffix().compare("csv",Qt::CaseInsensitive)==0) {reviewSignals({},QString::fromUtf8(input.readAll()));return;}
  QJsonParseError error;const auto document=QJsonDocument::fromJson(input.readAll(),&error);
  if(error.error!=QJsonParseError::NoError || !document.isObject()) {toast("Invalid ICD JSON");return;}
  if(document.object()["schema"].toString()=="aerotrace.icd.review.v1" && document.object()["draft"].toBool() && document.object()["rows"].isArray()) {
    reviewSignals(document.object()["rows"].toArray(),{},true);return;
  }
  if(document.object().value("draft").toBool()) {
    auto object=document.object();
    object.remove("draft");
    std::vector<aero::Signal> mappings;
    if(auto error=desktop::profileFromJson(object,mappings);!error.isEmpty()){toast(error);return;}
    reviewSignals(desktop::profileToJson(mappings)["signals"].toArray(),{},true);
    return;
  }
  activateProfile(document.object());
}

void Window::exports() {
  auto convert=new QPushButton("Open CSV Viewer…",content);pageLayout->addWidget(convert,0,Qt::AlignLeft);connect(convert,&QPushButton::clicked,this,[this]{csvTools();});
  auto form=showForm<Ui::ExportsPage>();
  auto report=new QPushButton("Export signal report…",content);
  titleActions->addWidget(report);
  connect(report,&QPushButton::clicked,this,[this]{exportSignalReport();});
  connect(form.createExportButton,&QPushButton::clicked,this,[this] { createExport(); });
  exportFormat=form.exportFormat; exportStart=form.exportStart; exportEnd=form.exportEnd;
  form.sourceName->setText(backend.replaySource ? QFileInfo(currentReplay).baseName() : backend.captureConfig.hardware ? "Current DDC capture" : "Current simulator capture");
  form.provenance->setText(backend.captureConfig.hardware ? "DDC MTI / device timestamps" : "Simulator");
  const auto &data=backend.replaySource ? backend.replayRecords : backend.archive;
  populate(form.previewTable,messageRows(data,3));
  form.available->setText(QString::number(data.size())); form.profile->setText(QString::fromStdString(backend.signal.id));
}


void Window::createExport() {
  if(backend.replaySource&&backend.recordingIndex&&backend.replayCount()>backend.replayRecords.size()) {
    toast("Use Sessions → Save selected session CSVs for the full recording. Large-recording engineering range export belongs to the next ICD phase.");return;
  }
  for(auto watcher:findChildren<QFutureWatcherBase *>()) if(watcher->isRunning()) {toast("One export is already running.");return;}
  const auto start=QTime::fromString(exportStart->text(),"HH:mm:ss.zzz");
  const auto end=QTime::fromString(exportEnd->text(),"HH:mm:ss.zzz");
  if(!start.isValid() || !end.isValid() || start>end) {
    toast("Enter a valid ordered start/end time range."); return;
  }
  const auto format=static_cast<aero::ExportFormat>(exportFormat->currentIndex());
  const auto path=QFileDialog::getSaveFileName(this,"Export dataset",sessionDir+(format==aero::ExportFormat::RawJson ? "/export.json" : "/export.csv"),format==aero::ExportFormat::RawJson ? "JSON (*.json)" : "CSV (*.csv)");
  if(path.isEmpty()) return;
  const auto &source=backend.replaySource ? backend.replayRecords : backend.archive;
  if(source.empty()) { toast("No recorded data. Start capture or open a session."); return; }
  auto watcher=new QFutureWatcher<aero::ExportResult>(this);
  connect(watcher,&QFutureWatcher<aero::ExportResult>::finished,this,[this,watcher] {
    const auto result=watcher->result();
    toast(result.error.empty() ? QString("Exported %1 records and SHA-256 manifest.").arg(result.count) : QString::fromStdString(result.error));
    watcher->deleteLater();
  });
  // Copy a bounded snapshot on the owner thread. The worker never accesses widgets,
  // settings, or mutable capture state and can finish safely after navigation.
  watcher->setFuture(QtConcurrent::run([records=source,mappings=backend.mappings(),example=backend.exampleContext(),format,path,startMs=start.msecsSinceStartOfDay(),endMs=end.msecsSinceStartOfDay()] {
    auto result=aero::Workbench::exportProfileRecords(records,mappings,format,startMs,endMs,example);
    if(!result.error.empty()) return result;
    const QByteArray output(result.text.data(),qsizetype(result.text.size()));
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(output)!=output.size() || !file.commit()) {
      result.error="Export failed: "+file.errorString().toStdString(); return result;
    }
    QSaveFile manifest(path+".sha256");
    const auto hash=QCryptographicHash::hash(output,QCryptographicHash::Sha256).toHex()+"  "+QFileInfo(path).fileName().toUtf8()+"\n";
    if(!manifest.open(QIODevice::WriteOnly) || manifest.write(hash)!=hash.size() || !manifest.commit())
      result.error="Data exported, but checksum manifest failed.";
    QSaveFile profile(path+".profile.json");
    if(!profile.open(QIODevice::WriteOnly) || profile.write(QJsonDocument(desktop::profileToJson(mappings)).toJson())<0 || !profile.commit()) result.error="Data exported, but ICD provenance sidecar failed.";
    result.text.clear();
    return result;
  }));
  toast("Export started in background.");
}
