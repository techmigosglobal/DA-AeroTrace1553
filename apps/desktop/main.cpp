#include "main_window.hpp"
#include "services/key_gate.hpp"
#include "widgets/startup_splash.hpp"
#include <exception>
#include <memory>
#include <optional>

namespace {
int reportStartupFailure(desktop::StartupSplash *splash,const QString &title,const QString &message) {
  if(splash)splash->close();
  QMessageBox::critical(nullptr,title,message);
  return 3;
}

int testStartupFailure(const QString &directory) {
  if(directory.isEmpty()||!QDir().mkpath(directory))return 2;
  desktop::StartupSplash splash(false);
  splash.show();
  splash.repaint();
  bool displayed=false;
  QTimer::singleShot(0,&splash,[&] {
    for(auto widget:QApplication::topLevelWidgets()) {
      if(auto message=qobject_cast<QMessageBox *>(widget);message&&message->isVisible()) {
        displayed=message->text()=="Fixture initialization failed"&&!splash.isVisible();
        message->accept();
      }
    }
  });
  const int result=reportStartupFailure(&splash,"Startup failed","Fixture initialization failed");
  QFile receipt(directory+"/startup-failure.txt");
  if(!receipt.open(QIODevice::WriteOnly|QIODevice::Text))return 2;
  receipt.write(displayed?"Error shown after splash closed\n":"Startup error was not shown correctly\n");
  return result==3&&displayed?0:1;
}

int testStartupSplash(QApplication &app,const QString &directory,bool slow=false) {
  if(directory.isEmpty() || !QDir().mkpath(directory))return 2;
  QSettings fixturePreferences(desktop::preferencesPath(),QSettings::IniFormat);
  fixturePreferences.setValue("reduceMotion",true);
  fixturePreferences.sync();
  const bool respectsPreference=!desktop::startupMotionEnabled();
  desktop::StartupSplash animated(true), stationary(desktop::startupMotionEnabled());
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
    animated.finishWhenReady([&,centered,motion,version,saved,animatedNext] {
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
          << "\nReduced-motion preference honored: " << respectsPreference
          << "\nVisible duration ms: " << duration << '\n';
    }
    const bool timing=duration>=desktop::StartupSplash::minimumVisibleMs&&
      (slow?duration>=5400:duration<6000);
    app.exit(centered&&motion&&version&&saved&&opened&&window->isVisible()&&respectsPreference&&timing?0:1);
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
    splash->setStage("Initializing diagnostic storage…");
    splash->show();
    splash->repaint();
  }
  if(!initializeApplicationLogging())
    return reportStartupFailure(splash?&*splash:nullptr,"Diagnostic storage unavailable",applicationLogError());
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
  const auto runEventLoop=[&app] {
    const int result=app.exec();
    applicationEvent("application_exit","",{{"exit_code",result}});
    flushApplicationLogging();
    return result;
  };
  if(splash) {
    splash->setStage("Loading operator workspace…");
    std::unique_ptr<Window> window;
    QTimer::singleShot(0,&app,[&] {
      try {window=std::make_unique<Window>();}
      catch(const std::exception &error) {
        app.exit(reportStartupFailure(&*splash,"Startup failed",QString::fromUtf8(error.what())));
        return;
      }
      splash->setStage("Preparing live workspace…");
      splash->finishWhenReady([&] {
        window->showMaximized();
        splash->close();
      });
    });
    return runEventLoop();
  }
  if(args.contains("--stable-workspace-test"))app.setApplicationName("AeroTrace611Checks");
  for(const auto &arg:args)if(arg.startsWith("--")&&arg.endsWith("-test")) {
    if(!args.contains("--stable-workspace-test"))app.setApplicationName("AeroTrace1553Fixtures");
    app.setProperty("recordingFixtureUi",true);break;
  }
  if(args.contains("--startup-failure-test"))return testStartupFailure(args.value(args.indexOf("--startup-failure-test")+1));
  if(args.contains("--startup-splash-test"))return testStartupSplash(app,args.value(args.indexOf("--startup-splash-test")+1),args.contains("--slow-startup"));
  if(args.contains("--manual-mapping-test"))
    app.setApplicationName("AeroTrace071Checks-"+QString::number(app.applicationPid()));
  const QStringList legacyIcdChecks={"--document-workflow-test","--demo-workflow-test",
    "--example-icd-workflow-test","--table-icd-workflow-test","--offline-icd-workflow-test"};
  app.setProperty("legacyIcdFixture",std::any_of(legacyIcdChecks.begin(),legacyIcdChecks.end(),
    [&](const QString &option){return args.contains(option);}));
  if(args.contains("--workflow-test") || args.contains("--smoke-test") || args.contains("--performance-test") || args.contains("--document-workflow-test") || (args.contains("--demo-workflow-test") || args.contains("--example-icd-workflow-test")) || args.contains("--operator-workflow-test")) QStandardPaths::setTestModeEnabled(true);
  Window window;
  window.show();
  if(!QStandardPaths::isTestModeEnabled()) window.showMaximized();
  if(args.contains("--manual-mapping-test")) {
    const auto i=args.indexOf("--manual-mapping-test");
    QTimer::singleShot(150,&window,[&,i]{app.exit(window.manualMappingTest(args.value(i+1),args.value(i+2),args.value(i+3))?0:1);});
  } else if(args.contains("--stable-workspace-test")) {
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
    window.setProperty("operatorReferencePdf",args.value(args.indexOf("--operator-workflow-test")+2));
    QTimer::singleShot(150,&window,[&,path]{app.exit(window.operatorWorkflowTest(path) ? 0 : 1);});
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
