#include "ddc_discovery.hpp"
#include "source.hpp"
#include "ddc_runtime.hpp"
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
QJsonObject discoverDdc(const QString &runtimePath) {
  QJsonObject report{{"schema","aerotrace.hardware-discovery.v1"},{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  const auto selection=aero::selectDdcRuntime(std::filesystem::path(runtimePath.toStdWString()));
  QJsonArray candidates;
  for(const auto &candidate:selection.candidates)candidates.append(QJsonObject{
    {"path",QString::fromStdWString(candidate.path.wstring())},{"origin",QString::fromStdString(candidate.origin)},
    {"file_version",QString::fromStdString(candidate.version)},{"api_raw",int(candidate.api)},
    {"usable",candidate.usable},{"driver_version_match",candidate.driverMatch},{"error",QString::fromStdString(candidate.error)},
    {"mt_receive_interface",candidate.legacyMt},{"mti_receive_interface",candidate.mti},
    {"missing_mt_exports",[&]{QJsonArray a;for(const auto &s:candidate.missingMt)a.append(QString::fromStdString(s));return a;}()},
    {"missing_mti_exports",[&]{QJsonArray a;for(const auto &s:candidate.missingMti)a.append(QString::fromStdString(s));return a;}()},
    {"compatibility_scope","Receive exports detected; SDK ABI, firmware, driver and hardware qualification remain separate"}});
  report["runtime_candidates"]=candidates;report["runtime_selection_error"]=QString::fromStdString(selection.error);
  const auto runtime=aero::inspectDdcRuntime(std::filesystem::path(runtimePath.toStdWString()));
  const auto path=QString::fromStdWString(runtime.path.wstring());
  report["runtime_available"]=runtime.available;report["runtime_message"]=QString::fromStdString(runtime.message);report["runtime_path"]=path;
  QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);if(file.open(QIODevice::ReadOnly)&&hash.addData(&file))report["runtime_sha256"]=QString::fromLatin1(hash.result().toHex());
  QJsonArray devices;
#ifdef Q_OS_WIN
  DWORD ignored=0;const auto bytes=GetFileVersionInfoSizeW(reinterpret_cast<LPCWSTR>(path.utf16()),&ignored);
  QByteArray version(bytes,0);
  if(bytes && GetFileVersionInfoW(reinterpret_cast<LPCWSTR>(path.utf16()),0,bytes,version.data())) {
    VS_FIXEDFILEINFO *info=nullptr;UINT size=0;
    if(VerQueryValueW(version.data(),L"\\",reinterpret_cast<void **>(&info),&size)&&info)report["runtime_file_version"]=QString("%1.%2.%3.%4").arg(HIWORD(info->dwFileVersionMS)).arg(LOWORD(info->dwFileVersionMS)).arg(HIWORD(info->dwFileVersionLS)).arg(LOWORD(info->dwFileVersionLS));
  }
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
  QProcess driverStore;driverStore.start(QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath("System32/pnputil.exe"),{"/enum-drivers","/class","{b8fd1ebc-0bfd-4973-9a61-258d72bda69f}"});
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
  QString text=r.value("runtime_message").toString()+"\nRuntime: "+r.value("runtime_path").toString()+"\nFile version: "+r.value("runtime_file_version").toString()+"\nSHA-256: "+r.value("runtime_sha256").toString()+"\n";
  const auto devices=r.value("devices").toArray();text+=r.value("physical_presence").toString()=="unknown"?"Windows hardware detection incomplete — presence unknown.\n":QString("Windows detected %1 matching DDC device(s).\n").arg(devices.size());
  for(const auto &v:devices){const auto d=v.toObject();text+=d.value("name").toString()+"\n  "+d.value("pnp_status").toString()+"; problem "+QString::number(d.value("pnp_problem").toInt())+"; service "+d.value("driver_service").toString()+"; driver "+d.value("driver_version").toString()+"\n  Installed INF: "+d.value("installed_inf").toString()+"\n  SDK match: "+d.value("matching_sdk_inf").toString()+"\n";}
  if(!r.value("discovery_error").toString().isEmpty())text+=r.value("discovery_error").toString()+"\n";
  if(r.contains("driver_store_error"))text+=r["driver_store_error"].toString()+"\n";
  for(const auto &value:r["runtime_candidates"].toArray()){
    const auto candidate=value.toObject();
    text+="Runtime candidate: "+candidate["path"].toString()+" · "+candidate["file_version"].toString()+" · "+candidate["origin"].toString()+
      (candidate["driver_version_match"].toBool()?" · matches installed driver":"")+
      (candidate["usable"].toBool()?QString():" · "+candidate["error"].toString())+"\n";
  }
  if(!r["runtime_selection_error"].toString().isEmpty())text+=r["runtime_selection_error"].toString()+"\n";
  const auto assignments=r.value("logical_assignments").toArray();
  text+=QString("Populated vendor assignment candidates (not hardware verification): %1\n").arg(assignments.size());
  text+=QString("Empty vendor slots: %1; malformed slots: %2\n").arg(r["empty_logical_slots"].toArray().size()).arg(r["malformed_logical_slots"].toArray().size());
  for(const auto &v:assignments)text+=QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact))+"\n";
  if(assignments.isEmpty())text+="No vendor logical assignments were found on this PC. Staged driver packages alone do not configure a logical channel.\n";
  for(const auto &v:r["malformed_logical_slots"].toArray())text+=QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact))+"\n";
  if(r.contains("logical_registry_error"))text+=r["logical_registry_error"].toString()+"\n";
  text+=r.value("logical_assignment").toString()+"\nFor -82, check the matching driver, logical assignments, access permissions, and whether another analyzer owns the device. These are possible causes until verified.";
  return text;
}
}
