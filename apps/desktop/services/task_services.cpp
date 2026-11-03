#include "task_services.hpp"
#include "profile_io.hpp"
#include "diagnostic_log.hpp"
#include <QtCore>
#include <cmath>
#include <limits>

namespace desktop {
QString writeJson(const QString &path,const QJsonObject &value) {
  QSaveFile file(path);const auto data=QJsonDocument(value).toJson();
  if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) return "Cannot save "+path;
  return {};
}
QString readJson(const QString &path,QJsonObject &value) {
  QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>4*1024*1024) return "Cannot read JSON (maximum 4 MiB): "+path;
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
  if(error.error!=QJsonParseError::NoError || !doc.isObject()) return "Invalid JSON: "+path;
  value=doc.object();return {};
}
QJsonArray checksToJson(const std::vector<aero::CheckRule> &rules) {
  QJsonArray result;for(const auto &r:rules) {QJsonObject o{{"signal",QString::fromStdString(r.signalId)},{"missing_ms",double(r.missingNs)/1e6},{"hysteresis",r.hysteresis}};if(r.minimum)o["minimum"]=*r.minimum;if(r.maximum)o["maximum"]=*r.maximum;result.append(o);}return result;
}
QString checksFromJson(const QJsonArray &values,const std::vector<aero::Signal> &profile,std::vector<aero::CheckRule> &result) {
  result.clear();if(values.size()>512) return "Maximum 512 checks";
  for(const auto &v:values) {
    if(!v.isObject()) return "Invalid check object";const auto o=v.toObject();aero::CheckRule r;r.signalId=o["signal"].toString().toStdString();
    for(const auto &key:{"minimum","maximum","hysteresis","missing_ms"}) if(o.contains(key) && (!o[key].isDouble() || !std::isfinite(o[key].toDouble()))) return "Invalid check number";
    if(o.contains("minimum")) r.minimum=o["minimum"].toDouble();if(o.contains("maximum")) r.maximum=o["maximum"].toDouble();
    const auto ms=o["missing_ms"].toDouble();if(ms<0 || ms>3600000) return "Missing timeout must be 0–3600000 ms";
    r.missingNs=std::uint64_t(ms*1e6);r.hysteresis=o["hysteresis"].toDouble();result.push_back(r);
  }
  return QString::fromStdString(aero::validateChecks(result,profile));
}
QJsonObject simulationToJson(const aero::CaptureConfig &c) {
  return {{"rate",int(c.simulatorRate)},{"wave",QString::fromStdString(c.simulatorWave)},{"amplitude",c.simulatorAmplitude},{"offset",c.simulatorOffset},
    {"frequency",c.simulatorFrequency},{"seed",QString::number(c.simulatorSeed)},{"drop_every",int(c.simulatorDropEvery)},{"flag_every",int(c.simulatorFlagEvery)}};
}
QString simulationFromJson(const QJsonObject &o,aero::CaptureConfig &c) {
  for(const auto &key:{"rate","amplitude","offset","frequency","drop_every","flag_every"}) if(!o[key].isDouble() || !std::isfinite(o[key].toDouble())) return "Missing/invalid simulator field: "+QString(key);
  const auto integer=[](double v){return v>=0 && v<=1000000 && std::floor(v)==v;};
  if(!integer(o["rate"].toDouble()) || !integer(o["drop_every"].toDouble()) || !integer(o["flag_every"].toDouble())) return "Invalid simulator count";
  bool ok=false;const auto seed=o["seed"].toString().toULongLong(&ok);if(!ok) return "Simulator seed must be an unsigned integer";
  c.simulatorRate=o["rate"].toInt();c.simulatorWave=o["wave"].toString().toStdString();c.simulatorAmplitude=o["amplitude"].toDouble();c.simulatorOffset=o["offset"].toDouble();c.simulatorFrequency=o["frequency"].toDouble();c.simulatorSeed=seed;c.simulatorDropEvery=o["drop_every"].toInt();c.simulatorFlagEvery=o["flag_every"].toInt();
  return QString::fromStdString(aero::validateSimulation(c));
}
QJsonObject batchAnalyze(const QStringList &paths,const QString &output,const std::vector<aero::Signal> &profile,const std::vector<aero::CheckRule> &rules,std::atomic<bool> *cancel,std::atomic<int> *progress) {
  QJsonArray results;const auto cancelled=[&]{return cancel && cancel->load();};
  if(profile.empty() || !aero::validateProfile(profile).empty() || !aero::validateChecks(rules,profile).empty()) return {{"error","Select a validated ICD and valid signal checks"}};
  if(paths.empty() || paths.size()>100) return {{"error","Choose 1–100 recordings"}};
  const auto directory=QDir(output).filePath("Batch_"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::Id128).left(8));
  if(!QDir().mkpath(directory)) return {{"error","Cannot create batch output directory"}};
  const auto profileJson=profileToJson(profile);const auto profileHash=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(profileJson).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
  applicationEvent("batch_started",directory,{{"inputs",paths.size()},{"profile_sha256",profileHash}});
  int complete=0,failed=0;
  for(int fileIndex=0;fileIndex<paths.size() && !cancelled();++fileIndex) {
    const auto &path=paths[fileIndex];QJsonObject item{{"input",QFileInfo(path).absoluteFilePath()},{"status","failed"}};
    auto process=[&]() -> QString {
      QFile input(path);if(!input.open(QIODevice::ReadOnly) || input.size()>32*1024*1024) return "Cannot read recording; current batch limit is 32 MiB per input";
      // Hash and decode the same immutable job snapshot, preventing a running capture from changing it between passes.
      const auto bytes=input.readAll();if(bytes.size()!=input.size()) return "Recording changed while reading";input.close();
      item["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
      QTemporaryFile snapshot;if(!snapshot.open() || snapshot.write(bytes)!=bytes.size() || !snapshot.flush()) return "Cannot create batch input snapshot";
      const auto loaded=aero::read(std::filesystem::path(snapshot.fileName().toStdWString()));
      if(!loaded.error.empty()) return QString::fromStdString(loaded.error);
      if(loaded.records.empty()) return "Recording contains no valid records";
      item["records"]=double(loaded.records.size());item["integrity"]=loaded.recovered ? "recovered_incomplete" : "crc_valid";
      struct Stats {std::uint64_t valid{},invalid{};double min{},max{};long double sum{};};std::vector<Stats> stats(profile.size());
      aero::SignalChecks checks;checks.configure(profile,rules);QJsonArray events;std::uint64_t eventCount=0;
      for(std::size_t index=0;index<loaded.records.size();++index) {
        if(index%256==0 && cancelled()) return "Cancelled";
        const auto &r=loaded.records[index];checks.consume(r);
        for(const auto &e:checks.drain()) {++eventCount;if(events.size()<10000) events.append(QJsonObject{{"timestamp_ns",QString::number(e.timestampNs)},{"record",QString::number(e.recordId)},{"channel",int(e.channel)},{"bus",e.bus},{"signal",QString::fromStdString(e.signalId)},{"kind",QString::fromStdString(e.kind)},{"detail",QString::fromStdString(e.detail)}});}
        const auto command=aero::interpret(r).payloadCommand;
        for(std::size_t i=0;i<profile.size();++i) {
          const auto &s=profile[i];if(s.rt!=command.rt || s.sa!=command.sa || s.transmit!=command.transmit || (s.channel && s.channel!=r.channel) || (s.bus>=0 && s.bus!=r.bus)) continue;
          const auto v=aero::decode(r,s);auto &st=stats[i];if(!v){++st.invalid;continue;}
          if(!st.valid)st.min=st.max=*v;else {st.min=std::min(st.min,*v);st.max=std::max(st.max,*v);}++st.valid;st.sum+=*v;
        }
      }
      QJsonArray mappings;for(std::size_t i=0;i<stats.size();++i) {const auto &s=stats[i];QJsonObject o{{"id",QString::fromStdString(profile[i].id)},{"unit",QString::fromStdString(profile[i].unit)},{"valid",double(s.valid)},{"invalid",double(s.invalid)}};if(s.valid){o["minimum"]=s.min;o["maximum"]=s.max;o["mean"]=double(s.sum/s.valid);}mappings.append(o);}
      const auto stem=QString::number(fileIndex+1)+"_"+QFileInfo(path).completeBaseName();const auto csv=QDir(directory).filePath(stem+".engineering.csv");
      QSaveFile target(csv);if(!target.open(QIODevice::WriteOnly))return "Cannot create engineering CSV";
      QCryptographicHash csvHash(QCryptographicHash::Sha256);std::uint64_t exportedRows=0;
      for(std::size_t start=0;start<loaded.records.size();start+=128) {
        if(cancelled())return "Cancelled";
        const auto finish=std::min(start+128,loaded.records.size());const std::vector<aero::Record> chunk(loaded.records.begin()+start,loaded.records.begin()+finish);
        const auto exported=aero::Workbench::exportProfileRecords(chunk,profile,aero::ExportFormat::EngineeringCsv,0,UINT64_MAX);
        // A chunk without matching signal rows still has a valid CSV header.
        if(!exported.error.empty() && exported.error!="No matching records")return QString::fromStdString(exported.error);
        auto data=QByteArray::fromStdString(exported.text);if(start){const auto line=data.indexOf('\n');if(line>=0)data.remove(0,line+1);}
        if(target.write(data)!=data.size())return "Engineering CSV write failed";csvHash.addData(data);exportedRows+=exported.count;
      }
      if(!target.commit())return "Cannot save engineering CSV";
      item["csv"]=csv;item["csv_sha256"]=QString::fromLatin1(csvHash.result().toHex());item["engineering_rows"]=double(exportedRows);item["signals"]=mappings;item["event_count"]=double(eventCount);item["events"]=events;item["events_truncated"]=eventCount>std::uint64_t(events.size());item["status"]=loaded.recovered ? "incomplete" : "complete";
      if(auto error=writeJson(QDir(directory).filePath(stem+".report.json"),item);!error.isEmpty())return error;
      ++complete;return {};
    };
    if(auto error=process();!error.isEmpty()) {item["error"]=error;item["status"]=error=="Cancelled" ? "cancelled" : "failed";++failed;}
    results.append(item);if(progress)progress->store(fileIndex+1);
    applicationEvent("batch_input_result",path,{{"status",item["status"]},{"error",item["error"]},{"records",item["records"]},{"csv",item["csv"]},{"sha256",item["sha256"]}});
  }
  QJsonObject report{{"schema","aerotrace.batch.v1"},{"output",directory},{"profile_sha256",profileHash},{"profile",profileJson},{"checks",checksToJson(rules)},{"profile_mode","explicit_selected_profile"},{"cancelled",cancelled()},{"processed",results.size()},{"requested",paths.size()},{"completed_files",complete},{"failed_files",failed},{"results",results}};
  if(auto error=writeJson(QDir(directory).filePath("batch-report.json"),report);!error.isEmpty())report["error"]=error;
  QString html="<!doctype html><html lang='en'><meta charset='utf-8'><title>AeroTrace batch report</title><style>body{font:15px system-ui;max-width:1100px;margin:40px auto;color:#272d29}table{border-collapse:collapse;width:100%;margin:20px 0}td,th{padding:9px;text-align:left;border-bottom:1px solid #ddd}code{overflow-wrap:anywhere}small{color:#62685f}</style><h1>AeroTrace batch analysis</h1>";
  html+=QString("<p>%1 / %2 inputs processed · %3 exported · %4 failed%5</p><p>Profile SHA-256: <code>%6</code></p><small>Selected approved ICD applied to every input. Recovered recordings are incomplete. Simulated records are labeled in the CSV. Detailed events, hashes and rules are in batch-report.json.</small>").arg(results.size()).arg(paths.size()).arg(complete).arg(failed).arg(cancelled()?" · cancelled":"").arg(profileHash);
  for(const auto &entry:results) {
    const auto item=entry.toObject();html+="<h2>"+QFileInfo(item["input"].toString()).fileName().toHtmlEscaped()+"</h2><p>"+item["status"].toString().toHtmlEscaped()+" · "+item["integrity"].toString().toHtmlEscaped()+"</p>";
    if(item.contains("error")){html+="<p>"+item["error"].toString().toHtmlEscaped()+"</p>";continue;}
    html+=QString("<p>%1 records · %2 engineering rows · %3 check transitions</p><table><thead><tr><th>Signal</th><th>Unit</th><th>Valid</th><th>Invalid</th><th>Min</th><th>Max</th><th>Mean</th></tr></thead><tbody>").arg(item["records"].toInt()).arg(item["engineering_rows"].toInt()).arg(item["event_count"].toInt());
    for(const auto &value:item["signals"].toArray()){const auto signal=value.toObject();html+="<tr>";for(const auto &key:{"id","unit","valid","invalid","minimum","maximum","mean"}){const auto v=signal[key];html+="<td>"+(v.isUndefined()?QString("—"):v.isString()?v.toString().toHtmlEscaped():QString::number(v.toDouble(),'g',10))+"</td>";}html+="</tr>";}html+="</tbody></table>";
  }
  QSaveFile human(QDir(directory).filePath("report.html"));const auto htmlBytes=html.toUtf8();if(!human.open(QIODevice::WriteOnly) || human.write(htmlBytes)!=htmlBytes.size() || !human.commit())report["error"]="Cannot write human-readable batch report";
  applicationEvent("batch_finished",directory,{{"cancelled",cancelled()},{"complete",complete},{"failed",failed},{"error",report.value("error")}});return report;
}
}

