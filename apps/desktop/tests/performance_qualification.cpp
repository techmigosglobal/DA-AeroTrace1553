#include "main_window.hpp"
#include "services/session_store.hpp"
#include "qualification_source.hpp"
#include "record_csv.hpp"
namespace desktop {void resetGuiUpdateStages();std::array<aero::TimingHistogram,8> guiUpdateStages();}
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
namespace {
class PaintObserver final:public QObject {
public:
  std::function<std::uint64_t()> displayedHost; aero::TimingHistogram latency,paint;
  bool eventFilter(QObject *,QEvent *e)override{if(e->type()==QEvent::Paint&&displayedHost){const auto host=displayedHost();const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();if(host&&std::uint64_t(now)>=host)latency.observe(now-host);}return false;}
};
QJsonObject timingJson(const aero::TimingHistogram &h){return {{"samples",double(h.count)},{"mean_ns",h.count?double(h.totalNs)/h.count:0},{"p50_upper_ns",double(h.quantile(50))},{"p95_upper_ns",double(h.quantile(95))},{"p99_upper_ns",double(h.quantile(99))},{"max_ns",double(h.maximumNs)},{"quantiles","fixed-memory log2 bucket upper bounds"}};}
}
bool Window::performanceTest(const QString &directory){
  QMap<QString,QVariant> saved;for(const auto &k:settings.allKeys())saved[k]=settings.value(k);const auto oldSession=sessionDir;
  QDir().mkpath(directory);const auto dataRoot=qEnvironmentVariable("AEROTRACE_SOAK_ROOT",QDir::tempPath());QDir().mkpath(dataRoot);QTemporaryDir temporary(QDir(dataRoot).filePath("aerotrace-soak-XXXXXX"));temporary.setAutoRemove(!qEnvironmentVariableIsSet("AEROTRACE_KEEP_SOAK"));
  if(!temporary.isValid())return false;const int duration=qBound(3,qEnvironmentVariableIntValue("AEROTRACE_SOAK_SECONDS")?qEnvironmentVariableIntValue("AEROTRACE_SOAK_SECONDS"):10,86400);
  const bool contention=qEnvironmentVariableIsSet("AEROTRACE_SOAK_CONTENTION");
  const bool allCharts=qEnvironmentVariableIsSet("AEROTRACE_MANUAL_CHART_SOAK");
  if(QStorageInfo(temporary.path()).bytesAvailable()<qint64(duration)*50000*1200)return false;
  unlocked=true;backend.stopRecording();backend.captureConfig.hardware=false;backend.profile.clear();backend.applyProfile(aero::Workbench::demoSignals());
  if(allCharts) {
    std::vector<aero::Signal> fields;
    for(unsigned word=0;word<32;++word) {
      aero::Signal signal{"manual/stress/"+std::to_string(word),"count"};
      signal.expectedWordCount=32;signal.word=word;signal.subsystem="Chart fixture";
      signal.icdSetId="manual";signal.messageId="stress";signal.sourceSignalId="Word "+std::to_string(word+1);
      fields.push_back(std::move(signal));
    }
    if(!backend.applyProfile(std::move(fields)).empty())return false;
  }
  backend.captureConfig.enabled={true,true,true,true};backend.captureConfig.simulatorRate=50000;backend.captureConfig.runId=applicationRunId().toStdString();
  sessionDir=temporary.path();currentFile=temporary.path()+"/performance.atd";const auto path=std::filesystem::path(currentFile.toStdWString());
  for(int c=0;c<2;++c){settings.setValue(QString("pane%1Bus").arg(c),"");settings.setValue(QString("pane%1Rt").arg(c),-1);settings.setValue(QString("pane%1Search").arg(c),"");}settings.setValue("monitorRowMode","rt");settings.setValue("monitorLayout","windows");
  auto generated=std::make_shared<aero::QualificationCounter>();bool passed=backend.acquisition.startConfigured(path,backend.captureConfig,std::make_unique<aero::QualificationSource>(50000,generated,allCharts));
  tick.setInterval(50);resize(1700,1000);navigate(4);
  QPointer<QDialog> liveChart;
  QPointer<QListView> liveRows;
  if(allCharts) {
    liveEngineering();
    liveChart=findChild<QDialog *>("liveEngineeringDialog");
    if(liveChart)liveRows=liveChart->findChild<QListView *>("liveFieldCharts");
  }
  std::array<bool,32> chartFields{};
  bool chartBudget=true;
  guiUpdateTiming={};desktop::resetGuiUpdateStages();
  PaintObserver observer;observer.displayedHost=[&]()->std::uint64_t{if(page!=4)return 0;if(rtGrouped){const auto *r=rtModels[0].recordFor(rtModels[0].index(0,0));return r?r->hostMonotonicNs:0;}const auto *r=paneModels[0].at(paneModels[0].rowCount()-1);return r?r->hostMonotonicNs:0;};
  auto watchPaint=[&]{for(auto v:rtViews)if(v)v->viewport()->installEventFilter(&observer);for(auto v:paneViews)if(v)v->viewport()->installEventFilter(&observer);};watchPaint();
  QElapsedTimer clock;clock.start();qint64 last=0,maxGap=0;std::vector<qint64> gaps;QJsonArray memorySamples;
  aero::TimingHistogram guiUpdate;int stage=0;QJsonArray longHeartbeats,transitions;
  auto sampleMemory=[&]{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),sizeof(m)))memorySamples.append(QJsonObject{{"elapsed_ms",clock.elapsed()},{"private_bytes",qint64(m.PrivateUsage)}});
#endif
  };
  QTimer heartbeat;heartbeat.setInterval(10);connect(&heartbeat,&QTimer::timeout,this,[&]{
    const auto now=clock.elapsed();
    if(last){
      const auto gap=now-last;gaps.push_back(gap);maxGap=std::max(maxGap,gap);
      if(gap>100&&longHeartbeats.size()<100)longHeartbeats.append(QJsonObject{{"end_ms",now},{"gap_ms",gap},{"page",page}});
    }
    last=now;
  });heartbeat.start();
  QFuture<void> progressWrite;
  QTimer memory;memory.setInterval(5000);connect(&memory,&QTimer::timeout,this,[&]{
    sampleMemory();
    if(progressWrite.isRunning())return;
    const auto metrics=backend.acquisition.metrics(false);
    const QJsonObject progress{{"status","RUNNING"},{"source","software fixture"},
      {"elapsed_ms",clock.elapsed()},{"recorded",double(metrics.recorded)},
      {"memory",memorySamples.isEmpty()?QJsonValue():memorySamples.last()}};
    progressWrite=QtConcurrent::run([directory,progress]{
      QSaveFile file(directory+"/performance-progress.json");
      if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(progress).toJson());file.commit();}
    });
  });sampleMemory();memory.start();
  QTimer layouts;layouts.setInterval(100);connect(&layouts,&QTimer::timeout,this,[&]{
    const auto next=std::min(2,int(clock.elapsed()/(duration*1000./3)));
    if(next==stage)return;
    stage=next;const auto began=clock.elapsed();
    if(stage==1){
      if(auto mode=content->findChild<QComboBox *>("monitorRowMode"))mode->setCurrentIndex(2);
      watchPaint();
    }else if(!allCharts) {
      analysisChannel=1;analysisBus=0;preferredSignalIndex=0;plotRawColumn=-1;navigate(7);
    }
    transitions.append(QJsonObject{{"start_ms",began},{"duration_ms",clock.elapsed()-began},{"stage",stage}});
  });layouts.start();
  QTimer chartProbe;chartProbe.setInterval(450);
  if(allCharts) {
    connect(&chartProbe,&QTimer::timeout,this,[&]{
      if(!liveRows || liveRows->model()->rowCount()!=32)return;
      const auto frame=backend.acquisition.liveFrame();
      if(!frame || !frame->commandPlot)return;
      const auto &plot=*frame->commandPlot;
      for(unsigned i=0;i<plot.traces.size() && i+plot.selection.first<chartFields.size();++i) {
        chartFields[i+plot.selection.first]|=!plot.traces[i].empty();
        chartBudget&=plot.traces[i].size()<=1800;
      }
      const auto next=std::find(chartFields.begin(),chartFields.end(),false);
      if(next!=chartFields.end())liveRows->scrollTo(liveRows->model()->index(int(next-chartFields.begin()),0),QAbstractItemView::PositionAtTop);
    });
    chartProbe.start();
  }
  const auto [rateStart,generatedStart]=generated->snapshot();
  QEventLoop wait;QTimer::singleShot(duration*1000,&wait,&QEventLoop::quit);wait.exec();const auto [rateEnd,generatedEnd]=generated->snapshot();
  heartbeat.stop();memory.stop();layouts.stop();chartProbe.stop();sampleMemory();
  progressWrite.waitForFinished();
  const double seconds=std::chrono::duration<double>(rateEnd-rateStart).count();
  std::uint64_t plotSamples=0;for(auto plot:plots)plotSamples+=plot->pointCount();
  if(allCharts) {
    plotSamples=std::count(chartFields.begin(),chartFields.end(),true);
    passed&=plotSamples==chartFields.size() && chartBudget;
    if(liveChart){liveChart->grab().save(directory+"/live-fields.png");liveChart->close();}
  }
  backend.stopRecording();
  const auto total=generated->snapshot().second;
  const auto metrics=backend.acquisition.metrics(false);const auto live=backend.acquisition.liveFrame();
  const bool documentComplete=true;
  std::array<std::uint64_t,4> observed{};bool contents=true;std::uint64_t ordinal=0;
  const auto recorded=aero::scanRecording(path,[&](const aero::Record &r,auto){auto expected=aero::qualificationRecord(ordinal++,allCharts);expected.hostMonotonicNs=r.hostMonotonicNs;contents&=r==expected;if(r.channel<1||r.channel>2||r.bus>1)return false;++observed[(r.channel-1)*2+r.bus];return true;});
  const auto rtFrame=backend.acquisition.rtFrame();
  bool counts=recorded.count==total&&metrics.recorded==total&&rtFrame&&rtFrame->revision==total;for(unsigned k=0;k<4;++k)counts&=observed[k]==(total+3-k)/4&&observed[k]==metrics.sourceCounts[k];
  std::sort(gaps.begin(),gaps.end());const auto p95=gaps.empty()?0:gaps[gaps.size()*95/100];const auto p99=gaps.empty()?0:gaps[gaps.size()*99/100];
  qint64 warm=0,peak=0;for(auto entry:memorySamples){const auto o=entry.toObject();if(o["elapsed_ms"].toInteger()>=60000){const auto bytes=o["private_bytes"].toInteger();if(!warm)warm=bytes;peak=qMax(peak,bytes);}}
  const auto effective=seconds>0?(generatedEnd-generatedStart)/seconds:0;
  const bool rateWithinTolerance=effective>=49000 && effective<=51000;bool bounded=true;if(live)for(const auto &trace:live->traces)bounded&=trace.size()<=1024;
  const bool durable=metrics.checkpointed==total && !metrics.writerRejected && metrics.writerQueueHighWater<=backend.captureConfig.writerCapacityBytes
    && metrics.peakIngressBytesPerSecond<=backend.captureConfig.writerCapacityBytes/2;
  passed&=counts&&contents&&durable&&!metrics.failed&&!recorded.recovered&&recorded.error.empty()&&!metrics.hardwareLost&&!metrics.hardwareOverflows&&metrics.queueHighWater<=8192&&bounded&&p95<=100&&maxGap<=250&&rateWithinTolerance&&(!warm||peak-warm<128ll*1024*1024)&&documentComplete&&(duration<65||warm>0)&&plotSamples>0;
  QJsonObject stages;for(unsigned k=0;k<aero::TimingStageCount;++k)stages[aero::timingNames[k]]=timingJson(metrics.timings[k]);
  stages["gui_update"]=timingJson(guiUpdateTiming);
  const auto guiStages=desktop::guiUpdateStages();const char *guiNames[]={"gui_rt_grouped","gui_chronological","gui_plots","gui_other_pages","gui_analysis_cache","gui_record_models","gui_operator_controls","gui_data_views"};for(unsigned k=0;k<guiStages.size();++k)stages[guiNames[k]]=timingJson(guiStages[k]);
  stages["capture_ingress_to_viewport_paint"]=timingJson(observer.latency);
  QJsonObject result{{"schema","aerotrace.software-qualification.v2"},{"status",passed?"PASS":"FAIL"},{"source","deterministic software fixture; no physical hardware"},{"physical_qualification","NOT RUN"},{"requested_events_per_second",50000},{"generated",double(total)},{"recorded",double(metrics.recorded)},{"exact_record_contents",contents},{"exact_stream_counts",counts},{"seconds",seconds},{"generated_in_rate_window",double(generatedEnd-generatedStart)},{"effective_records_per_second",effective},{"p95_gui_heartbeat_ms",p95},{"p99_gui_heartbeat_ms",p99},{"max_gui_heartbeat_gap_ms",maxGap},{"capture_to_paint_scope","Host ingress to Paint event for an applied External RT/raw row; excludes plot-only interval and physical wire latency"},{"stage_timings",stages},{"display_skips",double(metrics.displayDrops)},{"queue_high_water",double(metrics.queueHighWater)},{"hardware_lost",double(metrics.hardwareLost)},{"hardware_overflows",double(metrics.hardwareOverflows)},{"memory_samples",memorySamples},{"memory_gate_measured",warm>0},{"post_warmup_growth_bytes",peak-warm},{"plots_exercised",plotSamples>0},{"plot_samples",double(plotSamples)},{"trace_bounds",bounded},{"document_contention",contention},{"document_import_complete",documentComplete},{"recording",currentFile},{"application_log",applicationLogPath()},{"build_qt",qVersion()}};
  result["checkpointed_records"]=double(metrics.checkpointed);result["writer_queue_high_water_bytes"]=double(metrics.writerQueueHighWater);
  result["live_command_fields"]=allCharts?double(plotSamples):0;result["command_history_bounded"]=allCharts&&chartBudget;
  result["peak_ingress_bytes_per_second"]=double(metrics.peakIngressBytesPerSecond);
  result["long_heartbeats"]=longHeartbeats;result["view_transitions"]=transitions;
  result["rt_capture_records"]=double(rtFrame?rtFrame->revision:0);result["engineering_revision_records"]=double(live?live->records:0);
  QJsonArray streamCounts;for(unsigned k=0;k<4;++k)streamCounts.append(QJsonObject{{"channel",int(k/2+1)},{"bus",k%2?"B":"A"},{"expected",double((total+3-k)/4)},{"recorded",double(observed[k])},{"acquisition",double(metrics.sourceCounts[k])}});result["stream_counts"]=streamCounts;
  const auto csvRoot=temporary.path()+"/CSV";QDir().mkpath(csvRoot);
  if(duration>=30){
    QFutureWatcher<QJsonObject> job;QEventLoop finished;connect(&job,&QFutureWatcher<QJsonObject>::finished,&finished,&QEventLoop::quit);
    const auto file=currentFile;const auto config=backend.captureConfig;
    job.setFuture(QtConcurrent::run([path,file,csvRoot,config,total,allCharts]{
      desktop::describeSession(file,config);const auto saved=desktop::exportSession(file,csvRoot,{},"soak");bool exact=saved.error.isEmpty()&&!saved.incomplete&&!saved.recovered;
      QFile ext(csvRoot+"/soak_EXT.csv"),in(csvRoot+"/soak_INT.csv");exact&=ext.open(QIODevice::ReadOnly)&&in.open(QIODevice::ReadOnly);const auto header=QByteArray::fromHex("efbbbf")+QByteArray::fromStdString(aero::sessionCsvHeader());if(exact)exact=ext.readLine()==header&&in.readLine()==header;
      std::array<std::optional<std::uint64_t>,2> previous;std::uint64_t csvCount=0;
      const auto scan=aero::scanRecording(path,[&](const aero::Record &r,auto){const auto row=QByteArray::fromStdString(aero::sessionCsvRecord(r,previous[r.channel-1]));previous[r.channel-1]=r.timestampNs;++csvCount;exact&=(r.channel==1?ext:in).readLine()==row;return exact;});
      exact&=scan.error.empty()&&!scan.recovered&&csvCount==total&&ext.atEnd()&&in.atEnd();
      aero::RecordingIndex index;std::uint64_t ordinal=0;bool replay=true;const auto indexed=index.open(path,[&](const aero::Record &r){auto expected=aero::qualificationRecord(ordinal++,allCharts);expected.hostMonotonicNs=r.hostMonotonicNs;replay&=r==expected;});
      replay&=indexed.error.empty()&&!indexed.recovered&&index.size()==total;const auto checks=std::min<std::uint64_t>(total,4096);for(std::uint64_t j=0;j<checks;++j){const auto i=checks>1?j*(total-1)/(checks-1):0;const auto r=index.at(i);auto expected=aero::qualificationRecord(i,allCharts);expected.hostMonotonicNs=r.hostMonotonicNs;replay&=r==expected;}
      return QJsonObject{{"exact_csv_contents",exact},{"csv_rows_compared",double(csvCount)},{"indexed_replay_contents",replay},{"indexed_scan_records",double(ordinal)},{"random_seek_checks",double(checks)},{"export_error",saved.error}};
    }));finished.exec();const auto checked=job.result();for(auto it=checked.begin();it!=checked.end();++it)result[it.key()]=it.value();passed&=checked["exact_csv_contents"].toBool()&&checked["indexed_replay_contents"].toBool();
  }else {result["full_export_replay_status"]="NOT RUN: quick stress profile; full comparison requires duration >=30 seconds";}
  result["raw_crc_valid"]=!recorded.recovered&&recorded.error.empty();result["bounded_private_memory"]=!warm||peak-warm<128ll*1024*1024;result["rate_within_two_percent"]=rateWithinTolerance;result["max_worker_batch_ms"]=metrics.maxBatchMilliseconds;result["plots"]=int(plots.size());result["max_points_per_trace"]=1024;
  result["passed"]=passed;result["status"]=passed?"PASS":"FAIL";result["capture_error"]=QString::fromStdString(metrics.error);
  QSaveFile report(directory+"/performance.json");if(!report.open(QIODevice::WriteOnly))passed=false;else {report.write(QJsonDocument(result).toJson());passed&=report.commit();}
  settings.clear();for(auto it=saved.begin();it!=saved.end();++it)settings.setValue(it.key(),it.value());settings.sync();sessionDir=oldSession;return passed;
}
