#include "services/session_store.hpp"
#include "main_window.hpp"
#include "services/profile_io.hpp"
#include <QTemporaryDir>
#include "example_simulation.hpp"

namespace {
struct ReportInput {
  QString recording, directory, source="unverified legacy recording";
  std::vector<aero::Record> liveRecords;
  aero::Signal signal;
  int channel{}, bus{};
  bool example{};
};
struct ReportResult {
  QString error;
  QVector<QPointF> points;
  QJsonObject metadata;
};

QString csvCell(QString value) {
  return '"' + value.replace("\"", "\"\"") + '"';
}

// Compact adjacent buckets when the envelope fills, retaining extrema in record order.
class ReportEnvelope {
  struct Point { quint64 index; QPointF xy; };
  struct Bucket { Point low, high; };
  std::vector<Bucket> buckets;
  quint64 width{1}, count{};
  static void include(Bucket &bucket, const Point &point) {
    if(point.xy.y()<bucket.low.xy.y()) bucket.low=point;
    if(point.xy.y()>bucket.high.xy.y()) bucket.high=point;
  }
public:
  void add(QPointF xy) {
    const Point point{count,xy};
    if(count++%width==0) buckets.push_back({point,point});
    else include(buckets.back(),point);
    if(buckets.size()==2048 && count%width==0) {
      for(std::size_t index=0;index<buckets.size();index+=2) {
        auto combined=buckets[index];
        include(combined,buckets[index+1].low);
        include(combined,buckets[index+1].high);
        buckets[index/2]=combined;
      }
      buckets.resize(1024);
      width*=2;
    }
  }
  QVector<QPointF> points() const {
    QVector<QPointF> result;
    for(const auto &bucket:buckets) {
      const auto lowFirst=bucket.low.index<=bucket.high.index;
      result.append(lowFirst?bucket.low.xy:bucket.high.xy);
      if(bucket.low.index!=bucket.high.index) result.append(lowFirst?bucket.high.xy:bucket.low.xy);
    }
    return result;
  }
};

ReportResult writeSignalCsv(const ReportInput &input) {
  ReportResult result;
  QFile file(QDir(input.directory).filePath("engineering.csv"));
  if(!file.open(QIODevice::WriteOnly)) {result.error=file.errorString();return result;}
  if(file.write("record_id,timestamp_ns,channel,bus,signal_id,engineering_value,unit,quality\n")<0) {
    result.error=file.errorString();return result;
  }
  ReportEnvelope envelope;
  quint64 matching=0,valid=0,scanned=0;
  const auto id=csvCell(QString::fromStdString(input.signal.id));
  const auto unit=csvCell(QString::fromStdString(input.signal.unit));
  const auto consume=[&](const aero::Record &record) {
    ++scanned;
    if(record.channel!=unsigned(input.channel) || record.bus!=input.bus) return true;
    const auto decoded=aero::decodeValue(record,input.signal,input.example);
    if(!decoded.matched) return true;
    ++matching;
    QString quality="valid",value;
    if(decoded.value) {
      ++valid;
      value=QString::number(*decoded.value,'g',17);
      envelope.add({record.timestampNs/1e9,*decoded.value});
    } else quality=QString::fromUtf8(decoded.reason.data(),qsizetype(decoded.reason.size()));
    const auto row=QStringList{QString::number(record.id),QString::number(record.timestampNs),
      QString::number(record.channel),record.bus?"B":"A",id,value,unit,csvCell(quality)}.join(',').toUtf8()+'\n';
    if(file.write(row)!=row.size()) {result.error=file.errorString();return false;}
    return true;
  };
  bool recovered=false;
  if(input.recording.isEmpty()) {
    for(const auto &record:input.liveRecords) if(!consume(record)) break;
  } else {
    const auto scan=aero::scanRecording(std::filesystem::path(input.recording.toStdWString()),
      [&](const aero::Record &record,std::uint64_t){return consume(record);});
    recovered=scan.recovered;
    if(!scan.error.empty() && result.error.isEmpty()) result.error=QString::fromStdString(scan.error);
  }
  if(!file.flush() && result.error.isEmpty()) result.error=file.errorString();
  if(!result.error.isEmpty()) return result;
  if(!valid) {
    result.error=matching?"Matching records contained no valid engineering samples. Check the mapping and message quality.":
      "No matching traffic for this signal and stream in the selected data.";
    return result;
  }
  result.points=envelope.points();
  result.metadata={{"schema","aerotrace.signal-report.v1"},{"mapping",desktop::signalToJson(input.signal)},
    {"recording",QFileInfo(input.recording).fileName()},{"scope",input.recording.isEmpty()?"retained_live_snapshot":"full_saved_session"},
    {"profile_policy",input.recording.isEmpty()?"active profile at export start":"latest saved profile applied throughout"},
    {"source",input.source},{"example_simulation",input.example},{"recovered_recording",recovered},{"channel",input.channel},{"bus",input.bus?"B":"A"},
    {"scanned_records",QString::number(scanned)},{"matching_records",QString::number(matching)},
    {"valid_samples",QString::number(valid)},{"plotted_samples",result.points.size()}};
  return result;
}

QString publishSignalReport(const ReportResult &result,const QString &temporary,
                            const QString &destination,const QString &plotTitle) {
  QWidget chart;
  chart.setStyleSheet("background:white;color:#272d29;");
  auto layout=new QVBoxLayout(&chart);
  auto label=new QLabel(plotTitle,&chart);
  label->setTextFormat(Qt::PlainText);
  layout->addWidget(label);
  layout->addWidget(new QLabel("Stream time · decoded engineering values",&chart));
  auto plot=new SignalPlot(&chart);
  layout->addWidget(plot,1);
  chart.resize(1200,680);
  layout->activate();
  plot->setTimeAxis(true);
  plot->setSeries(result.points);
  const QDir staged(temporary);
  if(!chart.grab().save(staged.filePath("plot.png"))) return "Cannot save the report plot.";
  const auto json=QJsonDocument(result.metadata).toJson();
  QSaveFile manifest(staged.filePath("manifest.json"));
  if(!manifest.open(QIODevice::WriteOnly) || manifest.write(json)!=json.size() || !manifest.commit() ||
     !QDir().rename(temporary,destination)) {
    return "Cannot publish the report folder.";
  }
  return {};
}

}

void Window::exportSignalReport(QString suggestedId) {
  if(findChild<QFutureWatcherBase *>("signalReportJob")) {toast("A signal report is already running.");return;}
  bool accepted=false;
  const QStringList scopes{"Retained live snapshot","Full saved session"};
  const auto scope=QInputDialog::getItem(this,"Signal report","Data scope",scopes,backend.replaySource?1:0,false,&accepted);
  if(!accepted) return;
  ReportInput input;
  auto mappings=backend.captureMappings();
  if(scope==scopes[1]) {
    input.recording=QFileDialog::getOpenFileName(this,"Choose a saved recording",
      currentReplay.isEmpty()?sessionDir:currentReplay,"AeroTrace recording (*.atd)");
    if(input.recording.isEmpty()) return;
    if(backend.acquisition.active() && QFileInfo(input.recording)==QFileInfo(currentFile)) {
      toast("Stop and save this recording, or choose a retained live snapshot.");return;
    }
    QFile profile(input.recording+".profile.json");
    QJsonParseError syntax;
    if(!profile.open(QIODevice::ReadOnly) || profile.size()>4*1024*1024) {
      toast("This recording has no readable saved ICD profile.");return;
    }
    const auto object=QJsonDocument::fromJson(profile.readAll(),&syntax).object();
    const auto error=syntax.error==QJsonParseError::NoError?desktop::profileFromJson(object,mappings):syntax.errorString();
    if(!error.isEmpty()){toast(error);return;}
    input.example=desktop::verifiedOfflineFixture(input.recording,&input.source);
  }
  if(mappings.empty()){toast("The selected data has no ICD mappings.");return;}
  QDialog dialog(this);
  dialog.setWindowTitle("Export signal report");
  auto layout=new QFormLayout(&dialog);
  auto signal=new QComboBox(&dialog);
  for(const auto &mapping:mappings) signal->addItem(QString::fromStdString(mapping.id));
  if(const auto index=signal->findText(suggestedId);index>=0) signal->setCurrentIndex(index);
  layout->addRow("&Signal ID",signal);
  auto stream=new QComboBox(&dialog);
  stream->addItems({"CH1 External / A","CH1 External / B","CH2 Internal / A","CH2 Internal / B"});
  stream->setCurrentIndex((analysisChannel-1)*2+analysisBus);
  layout->addRow("Channel / &bus",stream);
  auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
  buttons->button(QDialogButtonBox::Ok)->setText("Choose output folder…");
  layout->addRow(buttons);
  connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  if(dialog.exec()!=QDialog::Accepted) return;
  const auto output=QFileDialog::getExistingDirectory(this,"Choose a parent folder for the signal report",sessionDir);
  if(output.isEmpty()) return;
  input.signal=mappings.at(signal->currentIndex());
  input.channel=stream->currentIndex()/2+1;
  input.bus=stream->currentIndex()%2;
  if(input.recording.isEmpty()) {
    input.liveRecords=backend.archive;
    input.example=backend.archiveExample;
    input.source=input.example?"bundled example simulation":
      (!input.liveRecords.empty() && input.liveRecords.front().source)?"DDC":"simulator";
    if(input.liveRecords.empty()){toast("No live capture data is retained.");return;}
  }
  auto temporary=std::make_shared<QTemporaryDir>(QDir(output).filePath(".signal-report-XXXXXX"));
  if(!temporary->isValid()){toast("Cannot create the report staging folder.");return;}
  input.directory=temporary->path();
  const auto destination=QDir(output).filePath("Signal-report-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8));
  const auto plotTitle=QString("%1 (%2) · %3").arg(signal->currentText(),QString::fromStdString(input.signal.unit),stream->currentText());
  auto watcher=new QFutureWatcher<ReportResult>(this);
  watcher->setObjectName("signalReportJob");
  connect(watcher,&QFutureWatcher<ReportResult>::finished,this,[this,watcher,temporary,destination,plotTitle]{
    const auto result=watcher->result();
    watcher->deleteLater();
    if(!result.error.isEmpty()){toast(result.error);return;}
    const auto error=publishSignalReport(result,temporary->path(),destination,plotTitle);
    if(!error.isEmpty()){toast(error);return;}
    toast("Signal report saved to "+destination);
  });
  watcher->setFuture(QtConcurrent::run([input=std::move(input),temporary]{
    try {return writeSignalCsv(input);}
    catch(const std::exception &error){return ReportResult{QString::fromUtf8(error.what())};}
  }));
  toast("Exporting signal report…");
}

bool Window::signalReportWorkflowTest(const QString &directory) {
  if(backend.profile.empty() || backend.archive.empty()) return false;
  QTemporaryDir temp;
  if(!temp.isValid()) return false;
  ReportInput input;
  input.directory=temp.path();
  input.liveRecords=backend.archive;
  input.signal=backend.profile.front();
  input.example=true;
  input.source="bundled example simulation";
  input.channel=1;
  const auto live=writeSignalCsv(input);
  if(!live.error.isEmpty() || live.points.isEmpty()) return false;
  const auto recording=temp.filePath("long.atd");
  aero::Recorder recorder;
  if(!recorder.open(std::filesystem::path(recording.toStdWString()))) return false;
  for(unsigned i=0;i<10000;++i) recorder.append(aero::exampleSimulationRecord(i,120,0));
  recorder.close();
  input.liveRecords.clear();
  input.recording=recording;
  const auto staged=temp.filePath("staged");
  if(!QDir().mkpath(staged)) return false;
  input.directory=staged;
  const auto saved=writeSignalCsv(input);
  if(!saved.error.isEmpty() || saved.metadata["valid_samples"]!="10000" ||
      saved.points.size()>=10000 || std::abs(saved.points.front().y()-100)>0.001) return false;
  const auto folder=temp.filePath("published");
  if(!publishSignalReport(saved,staged,folder,"32-word example").isEmpty()) return false;
  const QImage image(QDir(folder).filePath("plot.png"));
  if(image.isNull() || image.width()<1000) return false;
  const auto copy=QDir(directory).filePath("signal-report-plot.png");
  QFile::remove(copy);
  if(!QFile::copy(QDir(folder).filePath("plot.png"),copy)) return false;
  if(backend.profile.size()==19 && backend.profile.back().rt==8) {
    input.recording=temp.filePath("both.atd");
    if(!recorder.open(std::filesystem::path(input.recording.toStdWString()))) return false;
    aero::CaptureConfig fixture;fixture.enabled={true,false,false,false};
    for(unsigned i=0;i<32;++i) if(!recorder.append(aero::simulationRecord(fixture,i))) return false;
    if(!recorder.close()) return false;
    input.directory=temp.path();input.signal=backend.profile.back();
    const auto rt8=writeSignalCsv(input);
    if(!rt8.error.isEmpty() || rt8.metadata["matching_records"]!="16" ||
        rt8.metadata["valid_samples"]!="16" || rt8.points.front().y()!=490) return false;
  }
  input.directory=temp.path();
  input.channel=2;
  if(!writeSignalCsv(input).error.contains("No matching traffic")) return false;
  input.directory=temp.filePath("missing-folder");
  return !writeSignalCsv(input).error.isEmpty() && !QFileInfo::exists(input.directory);
}
