#include "ddc_discovery.hpp"
#include "card_manager.hpp"
#include "source.hpp"
#include "ddc_runtime.hpp"
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif
namespace desktop {
QString ddcDriverBinding(const QString &ids,const QString &service,const QString &inf,int problem,const QString &status) {
  if(service.isEmpty())return "Driver service not bound";
  if(ids.contains("USB\\VID_4DDC&PID_5590",Qt::CaseInsensitive)&&service.compare("e2mausb",Qt::CaseInsensitive))return "Unexpected service for E2MA USB — expected e2mausb";
  if(inf.isEmpty())return "Installed INF unknown — binding unverified";
  if(problem || status!="started")return "Driver/device not started successfully — inspect PnP problem";
  return "Bound and started in Windows; SDK access unverified";
}
bool ddcAssignmentMatchesDevice(const QString &assignmentId,const QString &instanceId) {
  const auto canonical=[](QString id){
    id=id.trimmed().toUpper();
    while(!id.isEmpty() && (id.front()=='\\' || id.front()=='?'))id.remove(0,1);
    id.replace('#','\\');const auto guid=id.indexOf("\\{");if(guid>=0)id.truncate(guid);
    return id;
  };
  const auto assigned=canonical(assignmentId),present=canonical(instanceId);
  return !assigned.isEmpty() && assigned==present;
}
QJsonObject classifyDdcAssignment(int logical,const QJsonObject &values) {
  QJsonObject entry{{"logical_device",logical},{"values",values}};
  if(values.isEmpty()){entry["state"]="empty";entry["reason"]="Vendor slot exists without assignment values";return entry;}
  QStringList missing;
  for(const auto &key:{"DeviceID","Channel","Family"})if(!values.contains(key)||values.value(key).isNull()||values.value(key).toVariant().toString().trimmed().isEmpty())missing<<key;
  if(!missing.isEmpty()){entry["state"]="malformed";entry["reason"]="Missing vendor fields: "+missing.join(", ");return entry;}
  // Available vendor headers specify names, not registry types or channel base.
  // Do not reject a vendor-created candidate using guessed representations.
  entry["state"]="candidate";entry["reason"]="Populated vendor fields; physical association and access require SDK verification";return entry;
}
QJsonArray ddcAssignmentChoices(const QJsonObject &report) {
  QJsonArray choices;
  for(const auto &value:report.value("logical_assignments").toArray()) {
    auto entry=value.toObject();const auto values=entry.value("values").toObject();
    const auto id=values.value("DeviceID").toString();bool channelOk=false;
    const auto physical=values.value("Channel").toVariant().toString().toInt(&channelOk);
    const auto number=entry.value("logical_device").toInt(-1);
    if(entry.value("state")!="candidate" || number<0||number>31 || id.isEmpty() || !channelOk || physical<0 || values.value("Remote").toVariant().toInt()!=0)continue;
    for(const auto &deviceValue:report.value("devices").toArray()) {
      const auto device=deviceValue.toObject();if(!ddcAssignmentMatchesDevice(id,device.value("instance_id").toString()))continue;
      entry["physical_channel"]=physical;entry["device_id"]=device.value("instance_id");entry["device_name"]=device.value("name");
      entry["label"]=QString("%1 · physical CH%2 · logical %3").arg(device.value("name").toString(device.value("instance_id").toString())).arg(physical+1).arg(entry.value("logical_device").toInt());
      choices.append(entry);break;
    }
  }
  return choices;
}
QJsonObject resolveDdcAssignments(const QJsonObject &report,const std::array<bool,4> &enabled,
                                 const std::array<int,2> &saved,const std::array<bool,2> &hasSaved,bool manual) {
  QJsonObject result;QJsonArray logical{saved[0],saved[1]},selected;const auto choices=ddcAssignmentChoices(report);
  auto reject=[&](const QString &message){result["valid"]=false;result["error"]=message;result["logical_devices"]=logical;return result;};
  if(!std::any_of(enabled.begin(),enabled.end(),[](bool v){return v;}))return reject("Select at least one channel and bus before connecting.");
  if(!manual && (!report.value("device_enumeration_complete").toBool() || report.contains("logical_registry_error")))
    return reject("Card Manager assignments could not be verified. Rescan DDC, inspect Windows/registry details, or deliberately select Advanced manual logical numbers.");
  for(int channel=0;channel<2;++channel) {
    if(!enabled[channel*2]&&!enabled[channel*2+1])continue;
    if(manual){if(saved[channel]<0||saved[channel]>31)return reject("Manual logical device numbers must be between 0 and 31.");continue;}
    QJsonArray candidates;for(const auto &value:choices){const auto c=value.toObject();if(hasSaved[channel]?c.value("logical_device").toInt()==saved[channel]:c.value("physical_channel").toInt()==channel)candidates.append(c);}
    if(candidates.size()!=1)return reject(hasSaved[channel]
      ?QString("CH%1 saved logical device %2 is missing, duplicated, or no longer associated with a present board. Choose its current Card Manager assignment.").arg(channel+1).arg(saved[channel])
      :QString("CH%1 has %2 matching Card Manager assignments. Select the board/channel explicitly; AeroTrace will not guess.").arg(channel+1).arg(candidates.size()));
    const auto chosen=candidates[0].toObject();logical[channel]=chosen.value("logical_device");selected.append(chosen);
  }
  if((enabled[0]||enabled[1])&&(enabled[2]||enabled[3])&&logical[0]==logical[1])return reject("The requested channels must use distinct Card Manager logical device numbers.");
  result["valid"]=true;result["logical_devices"]=logical;result["selected_assignments"]=selected;
  result["manual_unverified"]=manual;return result;
}
QString runtimeReadiness(const QJsonObject &report) {
  if(report.isEmpty())return "Not inspected";
  if(report.contains("runtime_probe_error"))return "SDK inspection incomplete — see Technical details";
  if(report.value("runtime_probe_blocked").toBool())return "Probe deferred — disconnect hardware before inspecting SDKs";
  if(report.value("provider_available").toBool())return report.value("automatic_approved").toBool()
    ?"Runtime and matching provider detected; channel connection not yet verified"
    :"Compatibility connection available; hardware qualification pending (NOT RUN)";
  if(report.value("runtime_detected").toBool())return report.value("runtime_loadable").toBool()
    ?"Runtime detected; matching provider unavailable"
    :"Runtime detected; loader or compatibility check failed";
  return "No runtime detected — see candidate evidence";
}
QJsonObject discoverDdc(const QString &runtimePath,bool probeRuntime) {
  QJsonObject report{{"schema","aerotrace.hardware-discovery.v2"},{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  report["runtime_available"]=false;
  aero::DdcRuntimeProbe inspection(probeRuntime);
  const bool blocked=!probeRuntime||!inspection.admitted();
  report["runtime_probe_blocked"]=blocked;
  if(!blocked)report["runtime_inspected_utc"]=report["utc"];
  if(!blocked) {
    QString inspector=QDir(QCoreApplication::applicationDirPath()).filePath("aerotrace_provider_info.exe");
#ifdef AEROTRACE_RUNTIME_INSPECTOR
    if(!QFileInfo::exists(inspector)&&!QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("deployment.json")))inspector=QString::fromUtf8(AEROTRACE_RUNTIME_INSPECTOR);
#endif
    if(!QFileInfo::exists(inspector))report["runtime_probe_error"]="The packaged SDK inspector is missing; repair the AeroTrace installation.";
    else {
      QProcess process;QByteArray output,errors;bool oversized=false;
      QObject::connect(&process,&QProcess::readyReadStandardOutput,[&]{output+=process.readAllStandardOutput();if(output.size()>4*1024*1024){oversized=true;process.kill();}});
      QObject::connect(&process,&QProcess::readyReadStandardError,[&]{errors+=process.readAllStandardError();if(errors.size()>65536)errors=errors.right(65536);});
      QStringList arguments{"--json"};if(runtimePath.trimmed().isEmpty())arguments<<"--inventory";else arguments<<runtimePath;
      process.start(inspector,arguments);
      if(!process.waitForStarted(3000))report["runtime_probe_error"]="SDK inspector could not start: "+process.errorString();
      else if(!process.waitForFinished(30000)){process.kill();process.waitForFinished(3000);report["runtime_probe_error"]="SDK inspection timed out after 30 seconds; no channel was opened.";}
      else if(oversized)report["runtime_probe_error"]="SDK inspector exceeded its bounded report size.";
      else {
        output+=process.readAllStandardOutput();QJsonParseError parse;
        const auto document=QJsonDocument::fromJson(output,&parse);const auto result=document.object();
        if(process.exitStatus()!=QProcess::NormalExit||parse.error!=QJsonParseError::NoError||result.value("schema")!="aerotrace.ddc-runtime-report.v2"||result.value("card_opened").toBool(true))
          report["runtime_probe_error"]="SDK inspector did not return a complete identity report: "+QString::fromUtf8(errors.right(4096));
        else {
          for(const auto &key:{"runtime_detected","runtime_loadable","provider_available","automatic_approved","runtime_path","provider_id","provider_path","qualification","runtime_selection_error","runtime_candidates"})report[key]=result.value(key);
          report["runtime_available"]=result.value("provider_available");report["runtime_message"]=result.value("message");report["runtime_stage"]=result.value("stage");
          report["runtime_file_version"]=result.value("file_version");report["runtime_sha256"]=result.value("sha256");report["runtime_architecture"]=result.value("architecture");
        }
      }
    }
  }else report["runtime_message"]="Runtime probing deferred while a provider connection exists. Windows inventory was refreshed without loading SDKs.";
  report["channel_checks"]=QJsonArray{QJsonObject{{"channel",1},{"state","not_checked"}},QJsonObject{{"channel",2},{"state","not_checked"}}};
  const auto manager=locateCardManager();report["card_manager_path"]=manager.path;report["card_manager_error"]=manager.error;
  QJsonArray devices;
#ifdef Q_OS_WIN
  QJsonArray assignments,emptySlots,malformedSlots;
  // This branch is read by the installed emacepl runtime and maintained by Card Manager.
  QSettings configuration("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\DDC1553Devices",QSettings::NativeFormat);
  HKEY assignmentKey{};const auto registryResult=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\DDC1553Devices",0,KEY_READ,&assignmentKey);
  if(registryResult==ERROR_SUCCESS)RegCloseKey(assignmentKey);
  else if(registryResult!=ERROR_FILE_NOT_FOUND)report["logical_registry_error"]=QString("Vendor registry read failed (%1); assignment state unknown").arg(registryResult);
  for(const auto &group:configuration.childGroups()) {
    bool number=false;const auto logical=group.toInt(&number);if(!number||logical<0||logical>31)continue;
    configuration.beginGroup(group);QJsonObject values,types;
    for(const auto &key:configuration.childKeys()){const auto value=configuration.value(key);values[key]=QJsonValue::fromVariant(value);types[key]=QString::fromLatin1(value.typeName()?value.typeName():"unknown");}
    configuration.endGroup();auto entry=classifyDdcAssignment(logical,values);entry["qt_value_types"]=types;const auto state=entry["state"].toString();
    if(state=="candidate")assignments.append(entry);else if(state=="empty")emptySlots.append(entry);else malformedSlots.append(entry);
  }
  report["logical_assignments"]=assignments;
  report["empty_logical_slots"]=emptySlots;report["malformed_logical_slots"]=malformedSlots;
  if(configuration.status()!=QSettings::NoError)report["logical_registry_error"]="Vendor registry could not be read completely; assignment state unknown";
  report["logical_registry_path"]="HKLM/SYSTEM/CurrentControlSet/Control/DDC1553Devices";
  // /enum-drivers /class requires Windows 11; keep inventory valid on Windows 10.
  QStringList driverArguments{"/enum-drivers"};
  if(QSysInfo::kernelVersion().section('.',2,2).toUInt()>=22000)driverArguments<<"/class"<<"{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}";
  QProcess driverStore;driverStore.start(QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath("System32/pnputil.exe"),driverArguments);
  if(driverStore.waitForFinished(15000)){report["driver_store_evidence"]=QString::fromLocal8Bit(driverStore.readAllStandardOutput());report["driver_store_exit_code"]=driverStore.exitCode();if(driverStore.exitCode()!=0)report["driver_store_error"]="Driver-store inventory failed; staging state unknown";}
  else {driverStore.kill();driverStore.waitForFinished();report["driver_store_error"]="PnPUtil driver inventory timed out";}
  struct Match {const char *id,*inf;};static const Match matches[]={
#include "ddc_hardware_ids.inc"
  };
  const auto set=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
  if(set==INVALID_HANDLE_VALUE)report["discovery_error"]=QString("Windows device enumeration failed (%1)").arg(GetLastError());
  else {
    report["device_enumeration_complete"]=true;
    SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
    for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&device);++i) {
      auto property=[&](DWORD key){DWORD type=0,required=0;SetupDiGetDeviceRegistryPropertyW(set,&device,key,&type,nullptr,0,&required);
        QByteArray value(required+2,0);if(!required||!SetupDiGetDeviceRegistryPropertyW(set,&device,key,&type,reinterpret_cast<PBYTE>(value.data()),required,nullptr))return QString();
        return QString::fromWCharArray(reinterpret_cast<const wchar_t *>(value.constData()),int(required/2)).replace(QChar(0),'\n').trimmed();};
      const auto ids=property(SPDRP_HARDWAREID).toUpper();QString inf;
      for(const auto &match:matches)for(const auto &id:ids.split('\n'))if(id==QLatin1String(match.id)||id.startsWith(QLatin1String(match.id)+"&")){inf=QString::fromLatin1(match.inf);break;}
      if(inf.isEmpty())continue;
      wchar_t instance[4096]{};SetupDiGetDeviceInstanceIdW(set,&device,instance,4096,nullptr);
      ULONG status=0,problem=0;const auto cm=CM_Get_DevNode_Status(&status,&problem,device.DevInst,0);
      auto name=property(SPDRP_FRIENDLYNAME);if(name.isEmpty())name=property(SPDRP_DEVICEDESC);
      QJsonObject row{{"name",name},{"instance_id",QString::fromWCharArray(instance)},{"hardware_ids",ids},{"driver_service",property(SPDRP_SERVICE)},
        {"pnp_status",cm==CR_SUCCESS?((status&DN_STARTED)?"started":"not_started"):"unknown"},{"pnp_problem",int(problem)},{"matching_sdk_inf","driver install/Win10/x64/"+inf}};
      const auto key=SetupDiOpenDevRegKey(set,&device,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);
      if(key!=INVALID_HANDLE_VALUE){for(const auto &entry:QList<QPair<QString,QString>>{{"driver_version","DriverVersion"},{"installed_inf","InfPath"},{"driver_provider","ProviderName"}}){wchar_t value[2048]{};DWORD size=sizeof(value),type=0;if(RegQueryValueExW(key,reinterpret_cast<LPCWSTR>(entry.second.utf16()),nullptr,&type,reinterpret_cast<LPBYTE>(value),&size)==ERROR_SUCCESS)row[entry.first]=QString::fromWCharArray(value);}RegCloseKey(key);}
      row["driver_binding"]=ddcDriverBinding(ids,row["driver_service"].toString(),row["installed_inf"].toString(),int(problem),row["pnp_status"].toString());
      devices.append(row);
    }
    const auto enumerationError=GetLastError();
    if(enumerationError!=ERROR_NO_MORE_ITEMS){report["device_enumeration_complete"]=false;report["discovery_error"]=QString("Windows enumeration incomplete (%1)").arg(enumerationError);}
    SetupDiDestroyDeviceInfoList(set);
  }
#else
  report["discovery_error"]="Windows SetupAPI discovery is unavailable on this platform";
#endif
  report["devices"]=devices;report["detected_devices"]=devices.size();
  report["assignment_choices"]=ddcAssignmentChoices(report);
  // Suggest only the local two-channel Card Manager mapping, never a sweep of SDK IDs.
  const auto assigned=report["logical_assignments"].toArray();
  if(devices.size()==1 && assigned.size()==2){
    const auto a=assigned[0].toObject(),b=assigned[1].toObject();
    const auto av=a["values"].toObject(),bv=b["values"].toObject();
    const auto channelA=av["Channel"].toVariant().toInt(),channelB=bv["Channel"].toVariant().toInt();
    const auto present=devices[0].toObject();
    if(report["device_enumeration_complete"].toBool() && !report.contains("logical_registry_error") &&
       ddcAssignmentMatchesDevice(av["DeviceID"].toString(),present["instance_id"].toString()) &&
       av["DeviceID"]==bv["DeviceID"] && av["Family"]==bv["Family"] &&
       av["Remote"].toVariant().toInt()==0 && bv["Remote"].toVariant().toInt()==0 &&
       ((channelA==0&&channelB==1)||(channelA==1&&channelB==0))){
      report["suggested_logical_devices"]=channelA==0?QJsonArray{a["logical_device"],b["logical_device"]}:QJsonArray{b["logical_device"],a["logical_device"]};
      report["assignment_selection"]="Single present device and one local Card Manager channel pair; SDK identity/open verification required";
    }
  }
  if(!report.contains("suggested_logical_devices"))report["assignment_selection"]="No unambiguous local two-channel mapping; preserve saved selection or select logical channels explicitly";

  report["physical_presence"]=report["device_enumeration_complete"].toBool()?(devices.isEmpty()?"absent":"detected"):"unknown";
  report["e2ma_usb_expected_id"]="USB\\VID_4DDC&PID_5590";
  report["e2ma_driver_service"]="e2mausb";
  report["monitor_interfaces"]="MT for legacy devices; MTI/Chapter10 for devices reporting MTI capability. Runtime availability does not prove either channel can initialize.";
  report["logical_assignment"]=report["assignment_selection"];
  return report;
}
QString discoverySummary(const QJsonObject &r) {
  QString text=runtimeReadiness(r)+"\n"+r.value("runtime_probe_error").toString()+"\n"+r.value("runtime_message").toString()+"\nRuntime: "+r.value("runtime_path").toString()+"\nArchitecture: "+r.value("runtime_architecture").toString()+"\nFile version: "+r.value("runtime_file_version").toString()+"\nSHA-256: "+r.value("runtime_sha256").toString()+"\nProvider: "+r.value("provider_id").toString()+"\nCard Manager: "+r.value("card_manager_path").toString()+"\n";
  const auto devices=r.value("devices").toArray();text+=r.value("physical_presence").toString()=="unknown"?"Windows hardware detection incomplete — presence unknown.\n":QString("Windows detected %1 matching DDC device(s).\n").arg(devices.size());
  for(const auto &v:devices){const auto d=v.toObject();text+=d.value("name").toString()+"\n  "+d.value("pnp_status").toString()+"; problem "+QString::number(d.value("pnp_problem").toInt())+"; service "+d.value("driver_service").toString()+"; driver "+d.value("driver_version").toString()+"\n  Installed INF: "+d.value("installed_inf").toString()+"\n  SDK match: "+d.value("matching_sdk_inf").toString()+"\n";}
  if(!r.value("discovery_error").toString().isEmpty())text+=r.value("discovery_error").toString()+"\n";
  if(r.contains("driver_store_error"))text+=r["driver_store_error"].toString()+"\n";
  for(const auto &value:r["runtime_candidates"].toArray()){
    const auto candidate=value.toObject();
    text+="Runtime candidate: "+candidate["path"].toString()+" · "+candidate["file_version"].toString()+" · "+candidate["origin"].toString()+
      (candidate["driver_version_match"].toBool()?" · matches installed driver":"")+
      " · "+candidate["stage"].toString()+(candidate["provider_id"].toString().isEmpty()?QString():" · provider "+candidate["provider_id"].toString())+
      (candidate["error"].toString().isEmpty()?QString():" · "+candidate["error"].toString())+"\n";
  }
  if(!r["runtime_selection_error"].toString().isEmpty())text+=r["runtime_selection_error"].toString()+"\n";
  const auto assignments=r.value("logical_assignments").toArray();
  text+=QString("Populated vendor assignment candidates (not hardware verification): %1\n").arg(assignments.size());
  text+=QString("Empty vendor slots: %1; malformed slots: %2\n").arg(r["empty_logical_slots"].toArray().size()).arg(r["malformed_logical_slots"].toArray().size());
  for(const auto &v:assignments)text+=QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact))+"\n";
  if(assignments.isEmpty())text+="No vendor logical assignments were found on this PC. Staged driver packages alone do not configure a logical channel.\n";
  for(const auto &v:r["malformed_logical_slots"].toArray())text+=QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact))+"\n";
  if(r.contains("logical_registry_error"))text+=r["logical_registry_error"].toString()+"\n";
  text+=r.value("logical_assignment").toString()+"\nFor -80, inspect the Card Manager logical-assignment registry and its read permissions. For -82, check the matching driver, logical assignments, access permissions, and whether another analyzer owns the device. These are possible causes until verified.";
  return text;
}
}
