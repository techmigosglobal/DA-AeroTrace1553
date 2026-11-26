#include "example_simulation.hpp"
#include "session_store.hpp"
#include "diagnostic_log.hpp"
#include "workbench.hpp"
#include "record_csv.hpp"
#include <limits>
#include <QTemporaryFile>
namespace desktop {
bool verifiedOfflineFixture(const QString &recording,QString *source) {
  QFile configuration(recording+".configuration.json"),status(recording+".status.txt");
  if(!configuration.open(QIODevice::ReadOnly) || configuration.size()>4*1024*1024) return false;
  const auto config=QJsonDocument::fromJson(configuration.readAll()).object();
  if(config["schema"]!="aerotrace.capture-config.v1" || !config["hardware"].isBool()) return false;
  if(source) *source=config["hardware"].toBool()?"DDC":"simulator";
  if(config["hardware"].toBool() || !status.open(QIODevice::ReadOnly) || status.size()>64*1024 ||
     status.readLine().trimmed()!="source=SIMULATOR") return false;
  const auto simulator=config["simulator"].toObject();
  if(simulator.contains("fixture_id"))
    return simulator["fixture_id"].toString()==QString::fromLatin1(aero::offlineFixtureId);
  return simulator["example_32_word"].toBool();
}

namespace {
QJsonObject readJson(const QString &p) {QFile f(p);if(f.open(QIODevice::ReadOnly))return QJsonDocument::fromJson(f.readAll()).object();return {};}
bool writeJson(const QString &p,const QJsonObject &o) {QSaveFile f(p);const auto b=QJsonDocument(o).toJson();return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
QString hashFile(const QString &p) {QFile f(p);QCryptographicHash h(QCryptographicHash::Sha256);if(!f.open(QIODevice::ReadOnly)||!h.addData(&f))return {};return QString::fromLatin1(h.result().toHex());}
QString allocatedName(const QJsonObject &o) {
  return o["run_name"].toString(o.contains("run_number")?"run"+o["run_number"].toVariant().toString():QString());
}
aero::CsvExportOptions storedOptions(const QJsonObject &o) {
  return {o["csv_layout"]=="compact11"?aero::CsvLayout::Compact11:aero::CsvLayout::Legacy42,
          static_cast<aero::NumericRadix>(o["csv_radix"].toInt(16))};
}
QString formatSuffix(aero::NumericRadix base) {
  switch(base){case aero::NumericRadix::Decimal:return "dec";case aero::NumericRadix::Binary:return "bin";case aero::NumericRadix::Octal:return "oct";default:return "hex";}
}
}
QString describeSession(const QString &p,const aero::CaptureConfig &c,const QString &runName,aero::CsvExportOptions options) {
  QJsonArray devices,enabled;for(auto n:c.logicalDevices)devices.append(n);for(auto on:c.enabled)enabled.append(on);
  QJsonObject o{{"schema","aerotrace.session.v1"},{"session_id",QFileInfo(p).baseName()},{"application_run_id",QString::fromStdString(c.runId)},
    {"source",c.hardware?"DDC_HARDWARE":"SIMULATED"},{"logical_devices",devices},{"enabled",enabled},{"external_channel",1},{"internal_channel",2},
    {"recording",QFileInfo(p).fileName()},{"started_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"export_state","not_exported"},
    {"receive_backend",c.backend==aero::DdcBackend::NativeX64?"native_x64":"helper_x86"},{"receive_mode",int(c.receiveMode)},
    {"connection_policy",c.connectionPolicy==aero::DdcConnectionPolicy::Compatibility?"compatibility":"qualified_automatic"},
    {"configured_runtime",QString::fromStdWString(c.runtime.wstring())},{"helper_manifest",QString::fromStdWString(c.helperPack.wstring())}};
  if(!runName.isEmpty()) {
    o["requested_run_name"]=runName;
    o["export_directory"]=QFileInfo(p).absolutePath();
    o["requested_csv_layout"]=options.layout==aero::CsvLayout::Compact11?"compact11":"legacy42";
    o["requested_csv_radix"]=int(options.radix);
  }
  return writeJson(p+".session.json",o)?QString():"Cannot preserve recording recovery marker";
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
SessionExportTarget sessionExportTarget(const QString &recording,const QString &directory,const QString &runName,aero::CsvExportOptions options,SessionExportIntent intent) {
  const auto primaryPath=recording+".session.json";
  const auto primary=readJson(primaryPath);
  const auto base=allocatedName(primary);
  const auto original=storedOptions(primary);
  if(base.isEmpty())return {runName.isEmpty()?suggestedRunName(directory):runName,QDir(directory).absolutePath(),primaryPath};
  const auto folder=primary["export_directory"].toString(directory);
  if(intent==SessionExportIntent::Retry || (options.layout==original.layout&&options.radix==original.radix))return {base,folder,primaryPath};
  const auto tag=formatSuffix(options.radix)+(options.layout==original.layout?QString():options.layout==aero::CsvLayout::Legacy42?"_legacy":"_compact");
  const auto variantPath=recording+".export-"+tag+".json";
  const auto variant=readJson(variantPath);
  if(!allocatedName(variant).isEmpty())return {allocatedName(variant),variant["export_directory"].toString(folder),variantPath};
  const auto entries=QDir(folder).entryList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot);
  auto occupied=[&](const QString &name){return entries.contains(name+"_EXT.csv",Qt::CaseInsensitive)||entries.contains(name+"_INT.csv",Qt::CaseInsensitive);};
  auto name=base.left(72-tag.size()-1)+"_"+tag;
  const auto stem=name;
  for(unsigned n=2;occupied(name);++n)name=stem+"_"+QString::number(n);
  return {name,folder,variantPath};
}
QList<SessionCsvSource> sessionCsvSources(const QString &recording) {
  const QFileInfo info(recording);
  auto manifests=info.dir().entryList({info.fileName()+".export-*.json"},QDir::Files);
  manifests.prepend(info.fileName()+".export.json");
  manifests.prepend(info.fileName()+".session.json");
  QList<SessionCsvSource> result;
  QSet<QString> included;
  for(const auto &manifest:manifests) {
    const auto item=readJson(info.dir().filePath(manifest));
    if(item["export_state"]!="complete")continue;
    const auto hashes=item["published_hashes"].toObject();
    for(const auto channel:{1u,2u}) {
      const auto path=item[channel==1?"external_csv":"internal_csv"].toString();
      const auto digest=hashes[path].toString();
      if(!path.isEmpty()&&digest.size()==64&&QFileInfo(path).isFile()&&!included.contains(path)){included.insert(path);result.append({path,digest,channel,storedOptions(item)});}
    }
  }
  return result;
}
QStringList recoverableSessions(const QStringList &roots) {
  QSet<QString> visited;QStringList result;
  for(const auto &root:roots) {
    QDirIterator files(root,{"*.atd"},QDir::Files,QDirIterator::Subdirectories);
    while(files.hasNext()) {
      const QFileInfo info(files.next());const auto path=info.canonicalFilePath();
      if(path.isEmpty()||visited.contains(path)||info.size()<=8)continue;visited.insert(path);
      const auto descriptor=readJson(path+".session.json");
      if(descriptor["export_state"]=="complete"||!sessionCsvSources(path).isEmpty())continue;
      // Check one CRC-valid record only. Full prefix validation belongs to export.
      const auto first=aero::scanRecording(std::filesystem::path(path.toStdWString()),[](const auto &,auto){return false;});
      if(first.error.empty()&&first.count>0)result<<path;
    }
  }
  std::sort(result.begin(),result.end(),[](const QString &a,const QString &b){return QFileInfo(a).lastModified()>QFileInfo(b).lastModified();});
  return result;
}
SessionSaveResult exportSession(const QString &recording,const QString &directory,const QString &captureError,const QString &runName,aero::CsvExportOptions options,
                                SessionExportIntent intent,const std::function<void(SessionSavePhase)> &progress,QString expectedName) {
  SessionSaveResult result;
  if(!QFileInfo::exists(recording)){result.error="No finalized recording exists. Connect and capture traffic first.";return result;}
  QLockFile recordingLock(recording+".export.lock");recordingLock.setStaleLockTime(0);if(!recordingLock.tryLock(0)){result.error="Another export is using this recording";return result;}
  const auto target=sessionExportTarget(recording,directory,runName,options,intent);
  if(!expectedName.isEmpty()&&target.name!=expectedName){result.error="Output names changed. Preview the export again";return result;}
  const bool variant=target.descriptor!=recording+".session.json";
  const auto actualDirectory=intent==SessionExportIntent::Retry?directory:target.directory;
  if(!QDir().mkpath(actualDirectory)){result.error="Cannot create CSV output directory";return result;}
  // Coordinate processes without leaving lock/counter/manifest files in the CSV folder.
  const auto lockRoot=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/ExportLocks";QDir().mkpath(lockRoot);
  const auto folderKey=QCryptographicHash::hash(QDir(actualDirectory).absolutePath().toCaseFolded().toUtf8(),QCryptographicHash::Sha256).toHex();
  QLockFile lock(lockRoot+"/"+folderKey+".lock");lock.setStaleLockTime(0);
  if(!lock.tryLock(0)){result.error="Another session export is using this folder";return result;}
  const auto descriptor=target.descriptor;auto session=readJson(descriptor);
  if(variant&&session.isEmpty()) {
    const auto original=readJson(recording+".session.json");
    for(const auto &key:{"session_id","application_run_id","source","logical_devices","enabled","started_utc","connection"})if(original.contains(key))session[key]=original[key];
  }
  // A session with only a valid header is legitimate, but a failed connection never creates an ATD file.
  auto validation=aero::scanRecording(std::filesystem::path(recording.toStdWString()),[](const auto &,auto){return false;});
  if(!validation.error.empty()){result.error=QString::fromStdString(validation.error);return result;}
  result.recovered=validation.recovered;
  const auto absolute=QDir(actualDirectory).absolutePath();
  result.runName=session.value("run_name").toString();
  if(!result.runName.isEmpty() || session.contains("run_number")) {
    options.layout=session.value("csv_layout").toString()=="compact11"?aero::CsvLayout::Compact11:aero::CsvLayout::Legacy42;
    options.radix=static_cast<aero::NumericRadix>(session.value("csv_radix").toInt(16));
  }
  result.run=session.value("run_number").toVariant().toULongLong();
  if(result.runName.isEmpty() && result.run)result.runName="run"+QString::number(result.run);
  if(!result.runName.isEmpty() && (session.value("export_directory").toString()!=absolute || (intent==SessionExportIntent::Retry&&!runName.isEmpty() && runName!=result.runName))) {result.error="This session already has an allocated name and folder. Retry with "+result.runName+" in "+session.value("export_directory").toString();return result;}
  if(result.runName.isEmpty()) {
    result.runName=target.name;
    if(auto error=validateRunName(result.runName);!error.isEmpty()){result.error=error;return result;}
    const auto occupied=QDir(absolute).entryList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot);
    for(const auto &name:occupied)if(name.compare(result.runName+"_EXT.csv",Qt::CaseInsensitive)==0 || name.compare(result.runName+"_INT.csv",Qt::CaseInsensitive)==0){result.error="Run name already exists. Choose a different name: "+result.runName;return result;}
    QRegularExpression numbered("^run([0-9]+)$",QRegularExpression::CaseInsensitiveOption);result.run=numbered.match(result.runName).captured(1).toULongLong();
    session["run_name"]=result.runName;session["run_number"]=QString::number(result.run);session["export_directory"]=absolute;session["recording"]=QFileInfo(recording).fileName();
    session["schema"]="aerotrace.session.v1";session["export_state"]="reserved";
    session["csv_layout"]=options.layout==aero::CsvLayout::Compact11?"compact11":"legacy42";session["csv_radix"]=int(options.radix);
    if(!writeJson(descriptor,session)){result.error="Cannot save run allocation; recording retained";return result;}
  }
  const auto prefix=QDir(actualDirectory).filePath(result.runName);
  const QStringList paths{session.value("external_csv").toString(prefix+"_EXT.csv"),session.value("internal_csv").toString(prefix+"_INT.csv")};
  auto owned=session.value("published_hashes").toObject();
  for(const auto &p:paths)if(QFileInfo::exists(p) && (owned.value(p).toString().isEmpty() || hashFile(p)!=owned.value(p).toString())) {
    result.error="Destination already exists or changed: "+p;return result;
  }
  if(progress)progress(SessionSavePhase::Verifying);
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
    result.incomplete=session.value("integrity").toString()!="validated";result.manifest=variant?descriptor:recording+".export.json";return result;
  }
  session["export_state"]="exporting";writeJson(descriptor,session);
  const QFileInfo before(recording);const auto sourceSize=before.size();const auto sourceModified=before.lastModified();
  if(progress)progress(SessionSavePhase::Writing);
  QTemporaryFile external(prefix+"_EXT.XXXXXX.tmp"),internal(prefix+"_INT.XXXXXX.tmp");
  if(!external.open()||!internal.open()){result.error="Cannot open both CSV destinations; recording retained";session["export_state"]="failed";session["export_error"]=result.error;writeJson(descriptor,session);return result;}
  const QByteArray header=QByteArray::fromHex("efbbbf")+QByteArray::fromStdString(aero::sessionCsvHeader(options));
  bool ok=external.write(header)==header.size() && internal.write(header)==header.size();
  QByteArray externalBuffer,internalBuffer;externalBuffer.reserve(262144);internalBuffer.reserve(262144);
  std::array<std::optional<std::uint64_t>,2> previousTimes;
  std::array<quint64,2> clockBackwards{};
  const auto scanned=aero::scanRecording(std::filesystem::path(recording.toStdWString()),[&](const aero::Record &r,auto){
    if(r.channel<1 || r.channel>2 || r.words.size()>32){ok=false;return false;}
    const auto channel=r.channel-1;
    if(previousTimes[channel] && r.timestampNs<*previousTimes[channel])++clockBackwards[channel];
    const auto bytes=QByteArray::fromStdString(aero::sessionCsvRecord(r,previousTimes[channel],options));
    previousTimes[channel]=r.timestampNs;
    auto &file=r.channel==1?external:internal;auto &buffer=r.channel==1?externalBuffer:internalBuffer;buffer+=bytes;
    if(buffer.size()>=262144){ok=ok && file.write(buffer)==buffer.size();buffer.clear();}
    if(r.channel==1)++result.external;else ++result.internal;return ok;
  });
  ok=ok&&external.write(externalBuffer)==externalBuffer.size()&&internal.write(internalBuffer)==internalBuffer.size();
  result.recovered|=scanned.recovered;
  if(!ok || !scanned.error.empty()){result.error="CSV write/read failed or recording changed; recording retained";}
  if(progress)progress(SessionSavePhase::Verifying);
  if(result.error.isEmpty()&&(!external.flush()||!internal.flush()))result.error="Cannot flush staged CSV files";
  const auto recordingHash=result.error.isEmpty()?hashFile(recording):QString();
  if(result.error.isEmpty()&&recordingHash.isEmpty())result.error="Cannot verify recording hash";
  const QFileInfo after(recording);
  if(after.size()!=sourceSize||after.lastModified()!=sourceModified)result.error="Recording changed during export; recording and existing CSVs retained";
  const auto publish=[&](QTemporaryFile &staged,const QString &path) {
    const auto temp=staged.fileName();staged.close();const auto digest=hashFile(temp);
    if(digest.isEmpty())return false;
    if(QFileInfo::exists(path))return digest==owned[path].toString()&&digest==hashFile(path);
    // Persist ownership before publication. A crash after rename remains retryable.
    owned[path]=digest;session["published_hashes"]=owned;
    if(!writeJson(descriptor,session))return false;
    // Rename from the same directory without replacing an existing file.
    if(!staged.rename(path))return false;
    staged.setAutoRemove(false);return true;
  };
  if(result.error.isEmpty()&&!publish(external,paths[0]))result.error="External CSV publication failed; recording retained";
  if(result.error.isEmpty()&&!publish(internal,paths[1]))result.error="Internal CSV publication failed; retry this export";
  QFile statusFile(recording+".status.txt");const auto statusAvailable=statusFile.open(QIODevice::ReadOnly);
  const auto status=statusAvailable?QString::fromUtf8(statusFile.readAll()):QString();
  QString recordedError;
  for(const auto &line:status.split('\n'))if(line.startsWith("error="))recordedError=line.mid(6).trimmed();
  const bool incomplete=result.recovered || !captureError.isEmpty() || !recordedError.isEmpty() || !status.contains("integrity=CLOSED");
  result.incomplete=incomplete;
  session["csv_schema"]=options.layout==aero::CsvLayout::Compact11?"aerotrace.compact-session-csv.v1":"aerotrace.vendor-session-csv.v1";session["csv_columns"]=options.layout==aero::CsvLayout::Compact11?11:42;
  session["csv_layout"]=options.layout==aero::CsvLayout::Compact11?"compact11":"legacy42";session["csv_radix"]=int(options.radix);session["csv_time_origin"]="relative card time; zero-based elapsed days";
  session["external_clock_backwards"]=QString::number(clockBackwards[0]);session["internal_clock_backwards"]=QString::number(clockBackwards[1]);session["csv_encoding"]="UTF-8 with BOM";
  session["csv_scope"]="Complete recording; display filters/retention do not limit export";
  session["integrity"]=incomplete?"incomplete":"validated";session["capture_error"]=recordedError.isEmpty()?captureError:recordedError;
  session["external_records"]=QString::number(result.external);session["internal_records"]=QString::number(result.internal);
  session["external_csv"]=paths[0];session["internal_csv"]=paths[1];session["recording_absolute"]=QFileInfo(recording).absoluteFilePath();
  if(!recordingHash.isEmpty())session["recording_sha256"]=recordingHash;
  session["completed_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  session["export_state"]=result.error.isEmpty()?"complete":"failed";session["export_error"]=result.error;
  QJsonArray associated;for(const auto &f:QDir(QFileInfo(recording).absolutePath()).entryList({QFileInfo(recording).fileName()+"*"},QDir::Files))if(!f.endsWith(".export.lock"))associated.append(f);
  session["session_files"]=associated;
  if(result.error.isEmpty()&&!variant) {
    result.manifest=recording+".export.json";
    if(!writeJson(result.manifest,session))result.error="CSV files saved, but export manifest failed; retry this session";
  }
  session["export_state"]=result.error.isEmpty()?"complete":"failed";session["export_error"]=result.error;
  bool descriptorSaved=writeJson(descriptor,session);
  // A brief Windows file share conflict can block the atomic replacement.
  // The export still owns the per-recording lock and its published file hashes.
  for(unsigned attempt=1;!descriptorSaved && attempt<=2;++attempt) {
    QThread::msleep(25*attempt);
    descriptorSaved=writeJson(descriptor,session);
  }
  if(!descriptorSaved && result.error.isEmpty())
    result.error="Exports saved, but the recording marker could not be updated. Retry the export.";
  if(result.error.isEmpty()&&variant)result.manifest=descriptor;
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
  for(const auto &pair:moved)if(pair.second.contains(".export-")&&pair.second.endsWith(".json")){
    auto variant=readJson(pair.second);variant["recording"]=QFileInfo(destination).fileName();variant["recording_absolute"]=QFileInfo(destination).absoluteFilePath();
    if(!writeJson(pair.second,variant))return "Files renamed; format-copy metadata update failed";
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
