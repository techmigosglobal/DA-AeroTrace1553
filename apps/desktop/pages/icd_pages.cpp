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
  form.configHeaderBar->setStyleSheet("background:#eaf5f0;border-bottom:1px solid #d4e6dd;border-top-left-radius:10px;border-top-right-radius:10px;");
  form.previewHeaderBar->setStyleSheet("background:#eaf5f0;border-bottom:1px solid #d4e6dd;border-top-left-radius:10px;border-top-right-radius:10px;");
  form.configurationTitle->setStyleSheet("font-size:16px;font-weight:700;color:#142d3a;");
  form.exportPreviewTitle->setStyleSheet("font-size:16px;font-weight:700;color:#142d3a;");
  form.formatLabel->setStyleSheet("font-size:13px;font-weight:600;color:#172b3a;");
  form.timeRangeHeading->setStyleSheet("font-size:14px;font-weight:700;color:#142d3a;margin-top:4px;");
  form.startLabel->setStyleSheet("font-size:12px;color:#4a6070;");
  form.endLabel->setStyleSheet("font-size:12px;color:#4a6070;");
  {
    auto cfgPm=QIcon(":/icons/settings.svg").pixmap(QSize(18,18),devicePixelRatioF());
    QPainter p(&cfgPm);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(cfgPm.rect(),QColor("#16804a"));p.end();
    form.configHeaderIcon->setPixmap(cfgPm);
    auto prevPm=QIcon(":/icons/search.svg").pixmap(QSize(18,18),devicePixelRatioF());
    QPainter p2(&prevPm);p2.setCompositionMode(QPainter::CompositionMode_SourceIn);p2.fillRect(prevPm.rect(),QColor("#16804a"));p2.end();
    form.previewHeaderIcon->setPixmap(prevPm);
  }
  form.exportStart->addAction(desktop::operatorIcon("restore"),QLineEdit::LeadingPosition);
  form.exportEnd->addAction(desktop::operatorIcon("restore"),QLineEdit::LeadingPosition);
  desktop::actionIcon(form.viewAsCsvButton,"file",false,15);
  desktop::actionIcon(form.createExportButton,"download",true,15);
  connect(form.viewAsCsvButton,&QPushButton::clicked,this,[this]{csvTools(backend.replaySource ? currentReplay : QString());});
  connect(form.closeExportsButton,&QPushButton::clicked,this,[this]{navigate(4);});
  connect(form.createExportButton,&QPushButton::clicked,this,[this] { createExport(); });
  exportFormat=form.exportFormat; exportStart=form.exportStart; exportEnd=form.exportEnd;

  const auto &data=backend.replaySource ? backend.replayRecords : backend.archive;
  const bool largeIndexed=backend.replaySource && backend.recordingIndex && backend.replayCount()>data.size();
  form.sourceName->setText(backend.replaySource ? QFileInfo(currentReplay).baseName() : backend.captureConfig.hardware ? "Current DDC capture" : "Current simulator capture");
  form.provenance->setText(backend.captureConfig.hardware ? "DDC MTI / device timestamps" : "Simulator");
  populate(form.previewTable,messageRows(data,3));
  auto emptyNotice=new desktop::TableNotice(form.previewTable,"database",44);
  emptyNotice->showState(form.previewTable->rowCount()==0,"No records to preview","Set the time range and export to view data.");
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

  auto fullRange=new QPushButton("Use full captured range",form.rangeActions);
  desktop::actionIcon(fullRange,"restore",false,15);
  form.rangeActionsLayout->addWidget(fullRange);
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
