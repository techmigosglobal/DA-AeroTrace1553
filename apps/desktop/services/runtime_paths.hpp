#pragma once
#include <QtCore>
namespace desktop {
inline QString dataDirectory(){return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);}
inline QString preferencesPath(){
  if(QStandardPaths::isTestModeEnabled())return dataDirectory()+"/Tests/"+QString::number(QCoreApplication::applicationPid())+"/preferences.ini";
  const auto legacy=QCoreApplication::applicationDirPath()+"/preferences.ini";
  return QFileInfo::exists(legacy)?legacy:dataDirectory()+"/preferences.ini";
}
inline QString defaultSessionDirectory(){return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/AeroSpace/Sessions";}
inline QString defaultExportDirectory(){return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/AeroSpace/Exports";}
}
