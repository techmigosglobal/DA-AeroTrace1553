#include "card_manager.hpp"
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace desktop {
CardManagerLocation resolveCardManager(const QStringList &candidates,const QString &explicitPath) {
  CardManagerLocation result;
  const auto paths=explicitPath.trimmed().isEmpty()?candidates:QStringList{explicitPath.trimmed()};
  QSet<QString> seen;
  for(auto path:paths) {
    path=QDir::fromNativeSeparators(path.trimmed());
    if(path.startsWith('"')&&path.endsWith('"'))path=path.mid(1,path.size()-2);
    const QFileInfo info(path);
    const auto key=QDir::cleanPath(info.absoluteFilePath()).toCaseFolded();
    if(seen.contains(key))continue;
    seen.insert(key);result.examined<<info.absoluteFilePath();
    if(info.isAbsolute()&&info.isFile()&&info.suffix().compare("exe",Qt::CaseInsensitive)==0) {
      result.path=info.canonicalFilePath();return result;
    }
  }
  result.error=explicitPath.trimmed().isEmpty()
    ?"DDC Card Manager was not found in registered or default installation locations. Select ddccm.exe in Technical details."
    :"The selected Card Manager executable is unavailable. Correct its path or clear the override to use automatic discovery.";
  return result;
}

CardManagerLocation locateCardManager(const QString &explicitPath) {
  QStringList candidates;
#ifdef Q_OS_WIN
  for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})for(const REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}) {
    HKEY key{};
    if(RegOpenKeyExW(hive,L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\ddccm.exe",0,KEY_READ|view,&key)==ERROR_SUCCESS) {
      wchar_t value[32768]{};DWORD type{},bytes=sizeof(value);
      if(RegQueryValueExW(key,nullptr,nullptr,&type,reinterpret_cast<LPBYTE>(value),&bytes)==ERROR_SUCCESS&&(type==REG_SZ||type==REG_EXPAND_SZ)) {
        value[32767]=0;
        wchar_t expanded[32768]{};
        const auto expandedLength=type==REG_EXPAND_SZ?ExpandEnvironmentStringsW(value,expanded,32768):0;
        if(expandedLength>0&&expandedLength<=32768)candidates<<QString::fromWCharArray(expanded);
        else candidates<<QString::fromWCharArray(value);
      }
      RegCloseKey(key);
    }
  }
  candidates<<"C:/DDC/ddccm.exe";
  if(!qEnvironmentVariable("DDC_SDK_ROOT").isEmpty()) {
    const QDir sdk(qEnvironmentVariable("DDC_SDK_ROOT"));
    candidates<<sdk.filePath("ddccm.exe")<<sdk.absoluteFilePath("../ddccm.exe");
  }
  for(const auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})for(const REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}) {
    HKEY uninstall{};
    if(RegOpenKeyExW(hive,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&uninstall)!=ERROR_SUCCESS)continue;
    wchar_t name[512]{};DWORD length=512;
    for(DWORD i=0;RegEnumKeyExW(uninstall,i,name,&length,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS;++i,length=512) {
      HKEY item{};
      if(RegOpenKeyExW(uninstall,name,0,KEY_READ,&item)!=ERROR_SUCCESS)continue;
      wchar_t title[1024]{},location[32768]{};DWORD bytes=sizeof(title),type{};
      RegQueryValueExW(item,L"DisplayName",nullptr,&type,reinterpret_cast<LPBYTE>(title),&bytes);
      title[1023]=0;
      if(QString::fromWCharArray(title).contains("aceXtreme",Qt::CaseInsensitive)||QString::fromWCharArray(title).contains("DDC Card Manager",Qt::CaseInsensitive)) {
        bytes=sizeof(location);
        if(RegQueryValueExW(item,L"InstallLocation",nullptr,&type,reinterpret_cast<LPBYTE>(location),&bytes)==ERROR_SUCCESS) {
          location[32767]=0;
          const QDir root(QString::fromWCharArray(location));candidates<<root.filePath("ddccm.exe")<<root.absoluteFilePath("../ddccm.exe");
        }
      }
      RegCloseKey(item);
    }
    RegCloseKey(uninstall);
  }
  // Vendor roots can contain a separate Card Manager installation alongside SDKs.
  for(const auto &entry:QDir("C:/DDC").entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot)) {
    if(entry.isSymLink())continue;
    candidates<<QDir(entry.absoluteFilePath()).filePath("ddccm.exe");
  }
  candidates<<QCoreApplication::applicationDirPath()+"/vendor/ddc/card-manager/ddccm.exe";
  candidates<<QDir(qEnvironmentVariable("SystemRoot","C:/Windows")).filePath("System32/ddccm.exe");
#endif
  return resolveCardManager(candidates,explicitPath);
}

CardManagerLaunch launchCardManager(const QString &path) {
  CardManagerLaunch result;
  const auto found=resolveCardManager({},path);
  if(found.path.isEmpty()){result.message=found.error;result.errorCode=2;return result;}
#ifdef Q_OS_WIN
  const auto directory=QFileInfo(found.path).absolutePath();
  SHELLEXECUTEINFOW request{};request.cbSize=sizeof(request);
  request.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
  request.lpVerb=L"runas";request.lpFile=reinterpret_cast<LPCWSTR>(found.path.utf16());
  request.lpDirectory=reinterpret_cast<LPCWSTR>(directory.utf16());request.nShow=SW_SHOWNORMAL;
  if(ShellExecuteExW(&request)) {
    result.launched=true;result.message="DDC Card Manager opened. Rescan assignments after making changes.";
    if(request.hProcess){result.processId=GetProcessId(request.hProcess);CloseHandle(request.hProcess);}
  } else {
    result.errorCode=GetLastError();result.cancelled=result.errorCode==ERROR_CANCELLED;
    result.message=result.cancelled?"Opening DDC Card Manager was cancelled at the Windows elevation prompt."
      :QString("DDC Card Manager could not open (Windows error %1). Executable: %2").arg(result.errorCode).arg(found.path);
  }
#else
  result.message="DDC Card Manager requires Windows.";
#endif
  return result;
}
}
