#include "main_window.hpp"
#include "ui_exports_page.h"
#include "services/profile_io.hpp"
#include "services/operator_style.hpp"

void Window::importIcdPath(const QString &path) {
  if(path.isEmpty()) return;
  if(QFileInfo(path).suffix().compare("pdf",Qt::CaseInsensitive)==0) {
    toast("PDF ICD libraries are disabled. Use Signal Mappings or import a JSON/CSV profile.");
    return;
  }
  QFile input(path);
  if(!input.open(QIODevice::ReadOnly) || input.size()>4*1024*1024) {toast("Cannot read signal profile (limit 4 MB)");return;}
  if(QFileInfo(path).suffix().compare("csv",Qt::CaseInsensitive)==0) {reviewSignals({},QString::fromUtf8(input.readAll()));return;}
  QJsonParseError error;const auto document=QJsonDocument::fromJson(input.readAll(),&error);
  if(error.error!=QJsonParseError::NoError || !document.isObject()) {toast("Invalid profile JSON");return;}
  if(document.object()["schema"].toString()=="aerotrace.icd.review.v1" && document.object()["draft"].toBool() && document.object()["rows"].isArray()) {
    reviewSignals(document.object()["rows"].toArray(),{},true);return;
  }
  if(document.object().value("draft").toBool()) {
    auto object=document.object();
    object.remove("draft");
    std::vector<aero::Signal> mappings;
    if(auto err=desktop::profileFromJson(object,mappings);!err.isEmpty()){toast(err);return;}
    reviewSignals(desktop::profileToJson(mappings)["signals"].toArray(),{},true);
    return;
  }
  activateProfile(document.object());
}

void Window::exports() {
  auto form=showForm<Ui::ExportsPage>();
  auto csvBtn=new QPushButton("Open CSV Viewer…",content);
  desktop::actionIcon(csvBtn,"file");
  titleActions->addWidget(csvBtn);
  connect(csvBtn,&QPushButton::clicked,this,[this]{csvTools(backend.replaySource ? currentReplay : QString());});

  auto report=new QPushButton("Export signal report…",content);
  desktop::actionIcon(report,"download");
  desktop::actionIcon(form.createExportButton,"save",form.createExportButton->property("primary").toBool());
  titleActions->addWidget(report);
  connect(report,&QPushButton::clicked,this,[this]{exportSignalReport();});
  connect(form.createExportButton,&QPushButton::clicked,this,[this] { createExport(); });
  exportFormat=form.exportFormat; exportStart=form.exportStart; exportEnd=form.exportEnd;

  const auto &data=backend.replaySource ? backend.replayRecords : backend.archive;
  const bool largeIndexed=backend.replaySource && backend.recordingIndex && backend.replayCount()>data.size();
  form.sourceName->setText(backend.replaySource ? QFileInfo(currentReplay).baseName() : backend.captureConfig.hardware ? "Current DDC capture" : "Current simulator capture");
  form.provenance->setText(backend.captureConfig.hardware ? "DDC MTI / device timestamps" : "Simulator");
  populate(form.previewTable,messageRows(data,3));
  const auto active=backend.mappings();
  form.available->setText(QString::number(data.size()));
  form.profile->setText(active.empty()?QString::fromStdString(backend.signal.id):QString("%1 active signal(s)").arg(active.size()));

  const auto fillBounds=[form,&data]{
    if(data.empty()) {
      form.exportStart->setText("00:00:00.000");
      form.exportEnd->setText("23:59:59.999");
      return;
    }
    const auto first=stamp(data.front().timestampNs).left(12);
    const auto last=stamp(data.back().timestampNs).left(12);
    if(QTime::fromString(first,"HH:mm:ss.zzz").isValid() && QTime::fromString(last,"HH:mm:ss.zzz").isValid()) {
      form.exportStart->setText(first);
      form.exportEnd->setText(last);
    }
  };
  fillBounds();

  auto fullRange=new QPushButton("Use full captured range",form.exportEnd->parentWidget());
  desktop::actionIcon(fullRange,"restore");
  if(auto *lay=qobject_cast<QVBoxLayout *>(form.exportEnd->parentWidget()->layout())) lay->addWidget(fullRange);
  connect(fullRange,&QPushButton::clicked,content,[this,form]{
    const auto &records=backend.replaySource ? backend.replayRecords : backend.archive;
    if(records.empty()){form.exportStart->setText("00:00:00.000");form.exportEnd->setText("23:59:59.999");return;}
    const auto first=stamp(records.front().timestampNs).left(12);
    const auto last=stamp(records.back().timestampNs).left(12);
    if(QTime::fromString(first,"HH:mm:ss.zzz").isValid() && QTime::fromString(last,"HH:mm:ss.zzz").isValid()) {
      form.exportStart->setText(first);
      form.exportEnd->setText(last);
    }
  });

  if(largeIndexed) {
    form.createExportButton->setText("Export loaded window");
    form.createExportButton->setToolTip("For full indexed sessions, use Open CSV Viewer… or Export signal report…");
    if(auto notice=content->findChild<QLabel *>("exportNotice")) {
      notice->setText(QString("Indexed recording (%1 total frames): time-bounded export covers the active memory window (%2 frames). Use Open CSV Viewer… or Export signal report… for full-file streaming exports.")
        .arg(backend.replayCount()).arg(data.size()));
    }
  }
}

void Window::createExport() {
  if(backend.replaySource&&backend.recordingIndex&&backend.replayCount()>backend.replayRecords.size()) {
    toast("Large indexed session: opening streaming CSV Viewer for full-file export…");
    csvTools(currentReplay);
    return;
  }
  for(auto watcher:findChildren<QFutureWatcherBase *>()) if(watcher->isRunning()) {toast("One export is already running.");return;}
  const auto start=QTime::fromString(exportStart->text(),"HH:mm:ss.zzz");
  const auto end=QTime::fromString(exportEnd->text(),"HH:mm:ss.zzz");
  if(!start.isValid() || !end.isValid() || start>end) {
    toast("Enter a valid ordered start/end time range (HH:mm:ss.zzz)."); return;
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
