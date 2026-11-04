#include "main_window.hpp"
#include "services/key_gate.hpp"
#include <exception>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("AeroTrace1553");
  app.setOrganizationName("AeroTrace");
  app.setApplicationVersion("0.6.0");
  auto args=app.arguments();for(const auto &arg:args)if(arg.startsWith("--") && arg.endsWith("-test"))QStandardPaths::setTestModeEnabled(true);
  if(!initializeApplicationLogging()){QMessageBox::critical(nullptr,"Diagnostic storage unavailable",applicationLogError());return 3;}
  installApplicationCrashHandler();
  installApplicationQtLogging();
  std::set_terminate([]{try{if(auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::exception &e){applicationEvent("unhandled_exception",QString::fromUtf8(e.what()));}catch(...){applicationEvent("unhandled_exception","Unknown C++ exception");}flushApplicationLogging();writeApplicationCrashDump();std::abort();});
  app.setWindowIcon(QIcon(":/icons/aerotrace.ico"));
  app.setStyle("Fusion");
  QPalette palette=app.palette();
  palette.setColor(QPalette::Window,QColor("#f3f3f0"));palette.setColor(QPalette::Base,Qt::white);
  palette.setColor(QPalette::Text,QColor("#272d29"));palette.setColor(QPalette::WindowText,QColor("#272d29"));
  palette.setColor(QPalette::Highlight,QColor("#dfeadf"));palette.setColor(QPalette::HighlightedText,QColor("#234d36"));
  palette.setColor(QPalette::Link,QColor("#28624d"));palette.setColor(QPalette::LinkVisited,QColor("#875b1f"));app.setPalette(palette);
  QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
  QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisb.ttf");
  QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisym.ttf");
  QFont font("Segoe UI");
  font.setPixelSize(15);
  app.setFont(font);
  QFile style(":/style.qss");
  if (style.open(QIODevice::ReadOnly))
    app.setStyleSheet(style.readAll());
  if(desktop::keyRequired())for(const auto &arg:args)if(arg.startsWith("--") && arg.endsWith("-test"))return 2;
  if(args.contains("--automation-test"))QStandardPaths::setTestModeEnabled(true);
  if(args.contains("--workflow-test") || args.contains("--smoke-test") || args.contains("--performance-test") || args.contains("--document-workflow-test") || args.contains("--demo-workflow-test") || args.contains("--operator-workflow-test")) QStandardPaths::setTestModeEnabled(true);
  Window window;
  window.show();
  if(!QStandardPaths::isTestModeEnabled()) window.showMaximized();
  if(args.contains("--rt-monitor-test")) {
    const auto path=args.value(args.indexOf("--rt-monitor-test")+1);QTimer::singleShot(150,&window,[&,path]{app.exit(window.rtMonitorWorkflowTest(path)?0:1);});
  } else if(args.contains("--hardware-connection-test")) {
    const auto i=args.indexOf("--hardware-connection-test");const auto path=args.value(i+1),runtime=args.value(i+2);
    QTimer::singleShot(150,&window,[&,path,runtime]{app.exit(window.hardwareConnectionTest(path,runtime)?0:1);});
  } else if(args.contains("--session-workflow-test")) {
    const auto path=args.value(args.indexOf("--session-workflow-test")+1);QTimer::singleShot(150,&window,[&,path]{app.exit(window.sessionWorkflowTest(path)?0:1);});
  } else if(args.contains("--automation-test")) {
    const auto path=args.value(args.indexOf("--automation-test")+1);QTimer::singleShot(150,&window,[&,path]{app.exit(window.automationWorkflowTest(path)?0:1);});
  } else if(args.contains("--operator-workflow-test")) {
    const auto path=args.value(args.indexOf("--operator-workflow-test")+1);
    QTimer::singleShot(150,&window,[&,path]{app.exit(window.operatorWorkflowTest(path) ? 0 : 1);});
  } else if(args.contains("--document-workflow-test") || args.contains("--demo-workflow-test")) {
    const auto expected=args.contains("--demo-workflow-test") ? 9u : 3u;
    const auto index=args.indexOf(expected==9 ? "--demo-workflow-test" : "--document-workflow-test");
    const auto path=args.value(index+1);const auto pdf=args.value(index+2);
    QTimer::singleShot(150,&window,[&,path,pdf,expected]{app.exit(window.documentWorkflowTest(path,pdf,expected) ? 0 : 1);});
  } else if(args.contains("--performance-test")) {
    const auto path=args.value(args.indexOf("--performance-test")+1,QCoreApplication::applicationDirPath()+"/test-results");
    QTimer::singleShot(150,&window,[&,path]{app.exit(window.performanceTest(path) ? 0 : 1);});
  } else if (args.contains("--workflow-test")) {
    QString path =
        args.value(args.indexOf("--workflow-test") + 1,
                   QCoreApplication::applicationDirPath() + "/test-results");
    QTimer::singleShot(150, &window, [&, path] {
      app.exit(window.workflowTest(path) ? 0 : 1);
    });
  } else if (args.contains("--smoke-test")) {
    int i = args.indexOf("--smoke-test");
    QString path = args.value(i + 1, QCoreApplication::applicationDirPath() +
                                         "/screenshots");
    QTimer::singleShot(150, &window, [&, path] {
      app.exit(window.smoke(path) ? 0 : 1);
    });
  }
  const int result=app.exec();applicationEvent("application_exit","",{{"exit_code",result}});flushApplicationLogging();return result;
}
