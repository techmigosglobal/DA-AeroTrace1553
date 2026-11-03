#include "session_store.hpp"
#include "diagnostic_log.hpp"
#include "workbench.hpp"
#include "record_csv.hpp"
#include <limits>
namespace desktop {
namespace {
QJsonObject readJson(const QString &p) {QFile f(p);if(f.open(QIODevice::ReadOnly))return QJsonDocument::fromJson(f.readAll()).object();return {};}
bool writeJson(const QString &p,const QJsonObject &o) {QSaveFile f(p);const auto b=QJsonDocument(o).toJson();return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
QString hashFile(const QString &p) {QFile f(p);QCryptographicHash h(QCryptographicHash::Sha256);if(!f.open(QIODevice::ReadOnly)||!h.addData(&f))return {};return QString::fromLatin1(h.result().toHex());}
}
QString describeSession(const QString &p,const aero::CaptureConfig &c) {
  QJsonArray devices,enabled;for(auto n:c.logicalDevices)devices.append(n);for(auto on:c.enabled)enabled.append(on);
  QJsonObject o{{"schema","aerotrace.session.v1"},{"session_id",QFileInfo(p).baseName()},{"application_run_id",QString::fromStdString(c.runId)},
    {"source",c.hardware?"DDC_HARDWARE":"SIMULATED"},{"logical_devices",devices},{"enabled",enabled},{"external_channel",1},{"internal_channel",2},
    {"recording",QFileInfo(p).fileName()},{"started_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"export_state","not_exported"}};
  return writeJson(p+".session.json",o)?QString():"Cannot preserve session descriptor";
}
QString sessionDisplayName(const QString &recording) {const auto o=readJson(recording+".session.json");return o.value("run_name").toString(QFileInfo(recording).baseName());}
QString validateRunName(const QString &name) {
  if(name.isEmpty() || name.size()>100 || name.endsWith('.') || name.endsWith(' ') || name.contains(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])")))return "Use a run name of 1–100 characters without Windows filename punctuation or trailing dots/spaces.";
  if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())return "This run name is reserved by Windows.";
  return {};
}
QString suggestedRunName(const QString &directory) {
  quint64 last=0;QRegularExpression pattern("^run([0-9]+)_(int|ext)\\.csv$",QRegularExpression::CaseInsensitiveOption);
  for(const auto &name:QDir(directory).entryList(QDir::Files)){const auto m=pattern.match(name);if(m.hasMatch())last=qMax(last,m.captured(1).toULongLong());}
  return last==std::numeric_limits<quint64>::max()?QString():"run"+QString::number(last+1);
}
SessionSaveResult exportSession(const QString &recording,const QString &directory,const QString &captureError,const QString &runName) {
  SessionSaveResult result;
  if(!QFileInfo::exists(recording)){result.error="No finalized recording exists. Connect and capture traffic first.";return result;}
  if(!QDir().mkpath(directory)){result.error="Cannot create CSV output directory";return result;}
  QLockFile recordingLock(recording+".export.lock");recordingLock.setStaleLockTime(0);if(!recordingLock.tryLock(0)){result.error="Another export is using this recording";return result;}
  // Coordinate processes without leaving lock/counter/manifest files in the CSV folder.
  const auto lockRoot=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ExportLocks";QDir().mkpath(lockRoot);
  const auto folderKey=QCryptographicHash::hash(QDir(directory).absolutePath().toCaseFolded().toUtf8(),QCryptographicHash::Sha256).toHex();
  QLockFile lock(lockRoot+"/"+folderKey+".lock");lock.setStaleLockTime(0);
  if(!lock.tryLock(0)){result.error="Another session export is using this folder";return result;}
  const auto descriptor=recording+".session.json";auto session=readJson(descriptor);
  // A session with only a valid header is legitimate, but a failed connection never creates an ATD file.
  auto validation=aero::scanRecording(std::filesystem::path(recording.toStdWString()),[](const auto &,auto){return false;});
  if(!validation.error.empty()){result.error=QString::fromStdString(validation.error);return result;}
  result.recovered=validation.recovered;
  const auto absolute=QDir(directory).absolutePath();
  result.runName=session.value("run_name").toString();
  result.run=session.value("run_number").toVariant().toULongLong();
  if(result.runName.isEmpty() && result.run)result.runName="run"+QString::number(result.run);
  if(!result.runName.isEmpty() && (session.value("export_directory").toString()!=absolute || (!runName.isEmpty() && runName!=result.runName))) {result.error="This session already has an allocated name and folder. Retry with "+result.runName+" in "+session.value("export_directory").toString();return result;}
  if(result.runName.isEmpty()) {
    result.runName=runName.isEmpty()?suggestedRunName(absolute):runName;
    if(auto error=validateRunName(result.runName);!error.isEmpty()){result.error=error;return result;}
    const auto occupied=QDir(absolute).entryList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot);
    for(const auto &name:occupied)if(name.compare(result.runName+"_EXT.csv",Qt::CaseInsensitive)==0 || name.compare(result.runName+"_INT.csv",Qt::CaseInsensitive)==0){result.error="Run name already exists. Choose a different name: "+result.runName;return result;}
    QRegularExpression numbered("^run([0-9]+)$",QRegularExpression::CaseInsensitiveOption);result.run=numbered.match(result.runName).captured(1).toULongLong();
    session["run_name"]=result.runName;session["run_number"]=QString::number(result.run);session["export_directory"]=absolute;session["recording"]=QFileInfo(recording).fileName();
    session["schema"]="aerotrace.session.v1";session["export_state"]="reserved";
    if(!writeJson(descriptor,session)){result.error="Cannot save run allocation; recording retained";return result;}
  }
  const auto prefix=QDir(directory).filePath(result.runName);
  const QStringList paths{session.value("external_csv").toString(prefix+"_EXT.csv"),session.value("internal_csv").toString(prefix+"_INT.csv")};
  auto owned=session.value("published_hashes").toObject();
  for(const auto &p:paths)if(QFileInfo::exists(p) && (owned.value(p).toString().isEmpty() || hashFile(p)!=owned.value(p).toString())) {
    result.error="Destination already exists or changed: "+p;return result;
  }
  if(session.value("export_state")=="complete" && QFileInfo::exists(paths[0]) && QFileInfo::exists(paths[1])) {
    if(hashFile(recording)!=session.value("recording_sha256").toString()){
      // The header-only preflight cannot see a damaged tail. Preserve the reason
      // for a rejected retry without touching the previously published pair.
      const auto changed=aero::scanRecording(std::filesystem::path(recording.toStdWString()),[](const auto &,auto){return true;});
      result.recovered=changed.recovered;result.incomplete=true;
      result.error="Recording changed since export; existing CSVs preserved";
      if(!changed.error.empty())result.error+="; "+QString::fromStdString(changed.error);
      return result;
    }
    result.external=session.value("external_records").toVariant().toULongLong();result.internal=session.value("internal_records").toVariant().toULongLong();
    result.incomplete=session.value("integrity").toString()!="validated";result.manifest=recording+".export.json";return result;
  }
  session["export_state"]="exporting";writeJson(descriptor,session);
  QSaveFile external(paths[0]),internal(paths[1]);
  if(!external.open(QIODevice::WriteOnly)||!internal.open(QIODevice::WriteOnly)){result.error="Cannot open both CSV destinations; recording retained";session["export_state"]="failed";session["export_error"]=result.error;writeJson(descriptor,session);return result;}
  const QByteArray header=QByteArray::fromHex("efbbbf")+QByteArray::fromStdString(aero::sessionCsvHeader());
  bool ok=external.write(header)==header.size() && internal.write(header)==header.size();
  QByteArray externalBuffer,internalBuffer;externalBuffer.reserve(262144);internalBuffer.reserve(262144);
  std::array<std::optional<std::uint64_t>,2> previousTimes;
  std::array<quint64,2> clockBackwards{};
  const auto scanned=aero::scanRecording(std::filesystem::path(recording.toStdWString()),[&](const aero::Record &r,auto){
    if(r.channel<1 || r.channel>2 || r.words.size()>32){ok=false;return false;}
    const auto channel=r.channel-1;
    if(previousTimes[channel] && r.timestampNs<*previousTimes[channel])++clockBackwards[channel];
    const auto bytes=QByteArray::fromStdString(aero::sessionCsvRecord(r,previousTimes[channel]));
    previousTimes[channel]=r.timestampNs;
    auto &file=r.channel==1?external:internal;auto &buffer=r.channel==1?externalBuffer:internalBuffer;buffer+=bytes;
    if(buffer.size()>=262144){ok=ok && file.write(buffer)==buffer.size();buffer.clear();}
    if(r.channel==1)++result.external;else ++result.internal;return ok;
  });
  ok=ok&&external.write(externalBuffer)==externalBuffer.size()&&internal.write(internalBuffer)==internalBuffer.size();
  result.recovered|=scanned.recovered;
  if(!ok || !scanned.error.empty()){result.error="CSV write/read failed or recording changed; recording retained";}
  if(result.error.isEmpty()) {
    if(!external.commit())result.error="External CSV commit failed; recording retained";
    else {owned[paths[0]]=hashFile(paths[0]);session["published_hashes"]=owned;if(!writeJson(descriptor,session))result.error="External CSV saved, but ownership metadata could not be saved";}
    if(result.error.isEmpty()) {
      if(!internal.commit())result.error="Internal CSV commit failed; external CSV saved; retry this session";
      else {owned[paths[1]]=hashFile(paths[1]);session["published_hashes"]=owned;if(!writeJson(descriptor,session))result.error="CSVs saved, but ownership metadata could not be saved";}
    }
  }
  QFile statusFile(recording+".status.txt");const auto statusAvailable=statusFile.open(QIODevice::ReadOnly);
  const auto status=statusAvailable?QString::fromUtf8(statusFile.readAll()):QString();
  QString recordedError;
  for(const auto &line:status.split('\n'))if(line.startsWith("error="))recordedError=line.mid(6).trimmed();
  const bool incomplete=result.recovered || !captureError.isEmpty() || !recordedError.isEmpty() || !status.contains("integrity=CLOSED");
  result.incomplete=incomplete;
  session["csv_schema"]="aerotrace.vendor-session-csv.v1";session["csv_columns"]=42;
  session["external_clock_backwards"]=QString::number(clockBackwards[0]);session["internal_clock_backwards"]=QString::number(clockBackwards[1]);session["csv_encoding"]="UTF-8 with BOM";
  session["csv_scope"]="Complete recording; display filters/retention do not limit export";
  session["integrity"]=incomplete?"incomplete":"validated";session["capture_error"]=recordedError.isEmpty()?captureError:recordedError;
  session["external_records"]=QString::number(result.external);session["internal_records"]=QString::number(result.internal);
  session["external_csv"]=paths[0];session["internal_csv"]=paths[1];session["recording_absolute"]=QFileInfo(recording).absoluteFilePath();
  session["recording_sha256"]=hashFile(recording);
  session["completed_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  session["export_state"]=result.error.isEmpty()?"complete":"failed";session["export_error"]=result.error;
  QJsonArray associated;for(const auto &f:QDir(QFileInfo(recording).absolutePath()).entryList({QFileInfo(recording).fileName()+"*"},QDir::Files))if(!f.endsWith(".export.lock"))associated.append(f);
  session["session_files"]=associated;
  if(result.error.isEmpty()) {
    result.manifest=recording+".export.json";
    if(!writeJson(result.manifest,session))result.error="CSV files saved, but export manifest failed; retry this session";
  }
  session["export_state"]=result.error.isEmpty()?"complete":"failed";session["export_error"]=result.error;
  if(!writeJson(descriptor,session) && result.error.isEmpty())result.error="Exports saved, but session descriptor update failed";
  applicationEvent("session_export_result",recording,{{"run",QString::number(result.run)},{"external",QString::number(result.external)},{"internal",QString::number(result.internal)},{"error",result.error},{"recovered",result.recovered},{"manifest",result.manifest}});
  return result;
}
QString renameRecording(const QString &source,const QString &destination) {
  if(!QFileInfo::exists(source))return "Source recording does not exist";
  if(QFileInfo::exists(destination))return "Destination already exists";
  const QFileInfo info(source);const auto files=info.dir().entryList({info.fileName(),info.fileName()+".*"},QDir::Files);
  QList<QPair<QString,QString>> moved;
  for(const auto &name:files) {
    const auto from=info.dir().filePath(name),to=destination+name.mid(info.fileName().size());
    if(QFileInfo::exists(to)||!QFile::rename(from,to)) {
      bool rollback=true;for(auto i=moved.rbegin();i!=moved.rend();++i)rollback=QFile::rename(i->second,i->first)&&rollback;
      return rollback?"Rename failed; original session restored":"Rename failed and rollback incomplete; check session files";
    }
    moved.append({from,to});
  }
  auto o=readJson(destination+".session.json");if(!o.isEmpty()) {
    o["recording"]=QFileInfo(destination).fileName();o["recording_absolute"]=QFileInfo(destination).absoluteFilePath();
    QJsonArray associated;for(const auto &pair:moved)associated.append(QFileInfo(pair.second).fileName());o["session_files"]=associated;
    if(!writeJson(destination+".session.json",o))return "Files renamed; session metadata update failed";
    if(QFileInfo::exists(destination+".export.json"))writeJson(destination+".export.json",o);
    if(!o.value("export_directory").toString().isEmpty()) {
      const auto manifest=QDir(o.value("export_directory").toString()).filePath("run"+o.value("run_number").toString()+".session.json");
      if(QFileInfo::exists(manifest)&&!writeJson(manifest,o))return "Files renamed; export manifest update failed";
    }
  }
  return {};
}
}
