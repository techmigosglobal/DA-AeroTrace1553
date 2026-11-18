#include "main_window.hpp"
#include "services/key_gate.hpp"
#include "widgets/startup_splash.hpp"
#include <exception>
#include <memory>
#include <optional>

namespace {
int testStartupSplash(QApplication &app,const QString &directory,bool slow=false) {
  if(directory.isEmpty() || !QDir().mkpath(directory))return 2;
  desktop::StartupSplash animated(true), stationary(false);
  animated.show();
  stationary.show();
  const auto animatedFirst=animated.grab();
  const auto staticFirst=stationary.grab();
  std::unique_ptr<Window> window;
  QTimer::singleShot(slow?5500:300,&app,[&] {
    const auto animatedNext=animated.grab();
    const auto staticNext=stationary.grab();
    const auto center=QGuiApplication::primaryScreen()->availableGeometry().center();
    const bool centered=(animated.geometry().center()-center).manhattanLength()<=2;
    const bool motion=animatedFirst.toImage()!=animatedNext.toImage() && staticFirst.toImage()==staticNext.toImage();
    window=std::make_unique<Window>();
    const bool version=window->windowTitle().contains(QCoreApplication::applicationVersion());
    const bool saved=animatedNext.save(directory+"/startup-animated.png") && staticNext.save(directory+"/startup-static.png");
    QTimer::singleShot(animated.remainingMinimumDuration(),Qt::PreciseTimer,&app,[&,centered,motion,version,saved,animatedNext] {
    window->show();
    const auto duration=animated.visibleElapsed();
    animated.close();
    stationary.close();
    QFile receipt(directory+"/startup-splash.txt");
    const bool opened=receipt.open(QIODevice::WriteOnly|QIODevice::Text);
    if(opened) {
      QTextStream out(&receipt);
      out << "Version: " << QCoreApplication::applicationVersion() << '\n'
          << "Device pixel ratio: " << animatedNext.devicePixelRatio() << '\n'
          << "Centered: " << centered << "\nAnimated and static frames: " << motion
          << "\nMain window visible after splash: " << window->isVisible()
          << "\nVisible duration ms: " << duration << '\n';
    }
    app.exit(centered && motion && version && saved && opened && window->isVisible() && duration>=desktop::StartupSplash::minimumVisibleMs?0:1);
    });
  });
  return app.exec();
}
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("AeroTrace1553");
  app.setOrganizationName("AeroTrace");
  app.setApplicationVersion(QStringLiteral(AEROTRACE_APPLICATION_VERSION));
  auto args=app.arguments();for(const auto &arg:args)if(arg.startsWith("--") && arg.endsWith("-test"))QStandardPaths::setTestModeEnabled(true);
  std::optional<desktop::StartupSplash> splash;
  if(!QStandardPaths::isTestModeEnabled()) {
    splash.emplace(desktop::startupMotionEnabled());
    splash->show();
    splash->repaint();
  }
  if(!initializeApplicationLogging()){
    if(splash)splash->close();
    QMessageBox::critical(nullptr,"Diagnostic storage unavailable",applicationLogError());
    return 3;
  }
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
  const auto runEventLoop=[&app] {
    const int result=app.exec();
    applicationEvent("application_exit","",{{"exit_code",result}});
    flushApplicationLogging();
    return result;
  };
  if(splash) {
    std::unique_ptr<Window> window;
    QTimer::singleShot(0,&app,[&] {
      try {window=std::make_unique<Window>();}
      catch(const std::exception &error){splash->close();QMessageBox::critical(nullptr,"Startup failed",QString::fromUtf8(error.what()));app.exit(3);return;}
      QTimer::singleShot(splash->remainingMinimumDuration(),Qt::PreciseTimer,&app,[&] {
      window->showMaximized();
      splash->close();
      QTimer::singleShot(0,window.get(),&Window::findUnsavedRecordings);
      });
    });
    return runEventLoop();
  }
  if(args.contains("--stable-workspace-test"))app.setApplicationName("AeroTrace611Checks");
  if(args.contains("--startup-splash-test"))return testStartupSplash(app,args.value(args.indexOf("--startup-splash-test")+1),args.contains("--slow-startup"));
  if(args.contains("--workflow-test") || args.contains("--smoke-test") || args.contains("--performance-test") || args.contains("--document-workflow-test") || (args.contains("--demo-workflow-test") || args.contains("--example-icd-workflow-test")) || args.contains("--operator-workflow-test")) QStandardPaths::setTestModeEnabled(true);
  Window window;
  window.show();
  if(!QStandardPaths::isTestModeEnabled()) window.showMaximized();
  if(args.contains("--stable-workspace-test")) {
    const auto path=args.value(args.indexOf("--stable-workspace-test")+1);QTimer::singleShot(150,&window,[&,path]{app.exit(window.stableWorkspaceTest(path)?0:1);});
  } else if(args.contains("--rt-monitor-test")) {
    const auto path=args.value(args.indexOf("--rt-monitor-test")+1);QTimer::singleShot(150,&window,[&,path]{app.exit(window.rtMonitorWorkflowTest(path)?0:1);});
  } else if(args.contains("--hardware-connection-test")) {
    const auto i=args.indexOf("--hardware-connection-test");const auto path=args.value(i+1),runtime=args.value(i+2);
    QTimer::singleShot(150,&window,[&,path,runtime]{app.exit(window.hardwareConnectionTest(path,runtime)?0:1);});
  } else if(args.contains("--session-workflow-test")) {
    const auto path=args.value(args.indexOf("--session-workflow-test")+1);QTimer::singleShot(150,&window,[&,path]{app.exit(window.sessionWorkflowTest(path)?0:1);});
  } else if(args.contains("--operator-workflow-test")) {
    const auto path=args.value(args.indexOf("--operator-workflow-test")+1);
    QTimer::singleShot(150,&window,[&,path]{app.exit(window.operatorWorkflowTest(path) ? 0 : 1);});
  } else if(args.contains("--table-icd-workflow-test")) {
    const auto index=args.indexOf("--table-icd-workflow-test");
    const auto path=args.value(index+1),pdf=args.value(index+2);
    QTimer::singleShot(150,&window,[&,path,pdf]{app.exit(window.documentWorkflowTest(path,pdf,54)?0:1);});
  } else if(args.contains("--offline-icd-workflow-test")) {
    const auto index=args.indexOf("--offline-icd-workflow-test");
    const auto path=args.value(index+1),pdf=args.value(index+2);
    QTimer::singleShot(150,&window,[&,path,pdf]{app.exit(window.documentWorkflowTest(path,pdf,19)?0:1);});
  } else if(args.contains("--document-workflow-test") || (args.contains("--demo-workflow-test") || args.contains("--example-icd-workflow-test"))) {
    const auto expected=args.contains("--example-icd-workflow-test") ? 18u : args.contains("--demo-workflow-test") ? 9u : 3u;
    const auto index=args.indexOf(expected==18 ? "--example-icd-workflow-test" : expected==9 ? "--demo-workflow-test" : "--document-workflow-test");
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
  return runEventLoop();
}
