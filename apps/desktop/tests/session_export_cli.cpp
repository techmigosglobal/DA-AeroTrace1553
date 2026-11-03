// Validation helper: uses the exact desktop complete-session export service.
#include "services/session_store.hpp"
#include "services/diagnostic_log.hpp"
#include <iostream>
int main(int argc,char **argv){
  QCoreApplication app(argc,argv);app.setOrganizationName("AeroTrace");app.setApplicationName("AeroTrace1553");QStandardPaths::setTestModeEnabled(true);
  const auto args=app.arguments();if(args.size()!=5){std::cerr<<"Usage: session_export_cli RECORDING OUTPUT_FOLDER RUN_NAME REPORT_JSON\n";return 2;}
  if(!initializeApplicationLogging())return 3;
  QElapsedTimer clock;clock.start();
  const auto result=desktop::exportSession(args[1],args[2],{},args[3]);
  auto countRows=[](const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return qint64(-1);qint64 count=-1;while(!file.atEnd()){const auto bytes=file.read(4*1024*1024);if(bytes.isEmpty()&&!file.atEnd())return qint64(-1);count+=bytes.count('\n');}return count;};
  const auto ext=countRows(QDir(args[2]).filePath(args[3]+"_EXT.csv")),in=countRows(QDir(args[2]).filePath(args[3]+"_INT.csv"));
  QJsonObject report{{"error",result.error},{"manifest",result.manifest},{"external_records",qint64(result.external)},{"internal_records",qint64(result.internal)},
    {"external_csv_rows",ext},{"internal_csv_rows",in},{"recovered",result.recovered},{"incomplete",result.incomplete},{"elapsed_ms",clock.elapsed()},
    {"logging_healthy",flushApplicationLogging()&&applicationLogError().isEmpty()}};
  const bool passed=result.error.isEmpty()&&!result.recovered&&!result.incomplete&&ext==qint64(result.external)&&in==qint64(result.internal)&&report["logging_healthy"].toBool();
  report["passed"]=passed;QSaveFile file(args[4]);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0||!file.commit())return 4;
  std::cout<<QJsonDocument(report).toJson().toStdString();shutdownApplicationLogging();return passed?0:1;
}
