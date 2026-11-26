#include "main_window.hpp"
#include "services/operator_style.hpp"
#include "services/runtime_paths.hpp"
#include "ui_diagnostics_page.h"
#include "ui_preferences_page.h"

void Window::preferences() {
    auto form = showForm<Ui::PreferencesPage>();
    form.interfaceLayout->insertWidget(0,desktop::sectionHeading("Interface","device"));
    form.captureCardLayout->insertWidget(0,desktop::sectionHeading("Capture & Plotting","activity"));
    form.safetyLayout->insertWidget(0,desktop::sectionHeading("Safety","shield"));
    form.pathsLayout->insertWidget(0,desktop::sectionHeading("Session Path","folder"));
    form.preferencesGrid->setRowStretch(0,1);
    form.preferencesGrid->setRowStretch(1,1);
    form.preferencesGrid->setContentsMargins(0,0,0,0);
    form.interfaceLayout->setContentsMargins(14,12,14,12);
    form.safetyLayout->setContentsMargins(14,12,14,12);
    form.captureCardLayout->setContentsMargins(14,12,14,12);
    form.pathsLayout->setContentsMargins(14,12,14,12);
    form.interfaceLayout->addStretch();
    form.safetyLayout->addStretch();
    for(auto card:{form.interfaceGroup,form.captureGroup,form.safetyGroup,form.pathsGroup})
      desktop::addCardShadow(card);
    desktop::actionIcon(form.restoreButton,"restore");
    desktop::actionIcon(form.saveButton,"save",true);
    desktop::actionIcon(form.browsePreferencesFolder,"folder");
    connect(form.restoreButton, &QPushButton::clicked, this, [this] {
    if (QMessageBox::question(this, "Restore defaults", "Reset all workspace preferences, channel selections, and layout settings to defaults?",
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    settings.clear();
    navigate(16);
  });
    connect(form.saveButton, &QPushButton::clicked, this, [this] { savePreferences(); });
    preferenceChecks = {form.autoScrollCheck, form.denseCheck, form.reduceMotionCheck};
    const QStringList keys = {"autoscroll", "dense", "reduceMotion"};
    for (int i = 0; i < preferenceChecks.size(); ++i) {
      preferenceChecks[i]->setProperty("key", keys[i]);
      preferenceChecks[i]->setChecked(settings.value(keys[i], i == 0).toBool());
    }
    monitorLayoutEdit=new QComboBox;monitorLayoutEdit->setObjectName("monitorLayoutSetting");monitorLayoutEdit->addItem("Movable workspace windows","windows");monitorLayoutEdit->addItem("Tabs","tabs");monitorLayoutEdit->setCurrentIndex(settings.value("monitorLayout","windows").toString()=="tabs"?1:0);
    form.captureLayout->addRow("Monitor layout",monitorLayoutEdit);
    form.captureCardLayout->addStretch();
    fpsEdit = form.fpsEdit;
    fpsEdit->setValue(settings.value("fps", 20).toInt());
    exportFolder = form.exportFolder;
    exportFolder->setAccessibleName("Recording output folder");
    exportFolder->setText(settings.value("exportFolder",desktop::defaultExportDirectory()).toString());
    exportFolder->setToolTip(exportFolder->text());
    connect(exportFolder,&QLineEdit::textChanged,exportFolder,&QLineEdit::setToolTip);
    connect(form.browsePreferencesFolder,&QPushButton::clicked,exportFolder,[this] {
      const auto path=QFileDialog::getExistingDirectory(this,"Recording output folder",exportFolder->text());
      if(!path.isEmpty())exportFolder->setText(path);
    });
}

void Window::savePreferences(){
    if(backend.acquisition.active() || connecting || savingSession){
      toast("Finish recording and saving before changing the output folder.");return;
    }
    auto csvDir=exportFolder->text().trimmed();
    if(csvDir.isEmpty()||!QDir().mkpath(csvDir)){toast("Choose a writable recording output folder.");return;}
    sessionDir = QDir(csvDir).absolutePath();
    settings.setValue("recordFolder", sessionDir);
    settings.setValue("exportFolder",sessionDir);
    settings.setValue("fps", fpsEdit->value());
    settings.setValue("monitorLayout",monitorLayoutEdit->currentData());
    for (auto c : preferenceChecks)
      settings.setValue(c->property("key").toString(), c->isChecked());
    settings.sync();
    tick.setInterval(1000 / fpsEdit->value());
    toast("Preferences saved locally.");
  }

void Window::diagnostics() {
  auto form=showForm<Ui::DiagnosticsPage>();
  form.version->setText(QCoreApplication::applicationVersion());
  form.systemLayout->insertWidget(0,desktop::sectionHeading("System Information","device"));
  form.checksLayout->insertWidget(0,desktop::sectionHeading("Troubleshooting Checks","settings"));
  for(auto card:{form.system,form.checks}) desktop::addCardShadow(card);
  auto setupTile=[this](QPushButton *btn,const QString &iconName,const QString &caption,bool highlight){
    btn->setAccessibleName(caption);
    btn->setText(QString());
    btn->setFixedHeight(86);
    btn->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    btn->setStyleSheet(highlight
      ? "QPushButton{background:#e3f3eb;border:1px solid #b5dcc8;border-radius:10px;padding:8px;min-height:70px;} QPushButton:hover{background:#d6ede1;}"
      : "QPushButton{background:#ffffff;border:1px solid #d4e0e9;border-radius:10px;padding:8px;min-height:70px;} QPushButton:hover{background:#f2f7fa;}");
    auto lay=new QVBoxLayout(btn);
    lay->setContentsMargins(8,10,8,10);lay->setSpacing(6);lay->setAlignment(Qt::AlignCenter);
    auto ico=new QLabel(btn);ico->setFixedSize(28,28);ico->setAlignment(Qt::AlignCenter);
    auto pm=QIcon(":/icons/"+iconName+".svg").pixmap(QSize(28,28),devicePixelRatioF());
    QPainter p(&pm);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(pm.rect(),QColor("#145331"));p.end();
    ico->setPixmap(pm);
    auto lbl=new QLabel(caption,btn);lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("font-size:13px;font-weight:600;color:#142d3a;");
    lay->addWidget(ico,0,Qt::AlignHCenter);lay->addWidget(lbl,0,Qt::AlignHCenter);
  };
  setupTile(form.selfTestButton,"activity","Run Self Test",true);
  setupTile(form.collectBundleButton,"folder","Collect Support Bundle",false);
  setupTile(form.openLogsButton,"file","Open Logs",false);
  setupTile(form.exportDiagnosticsButton,"download","Export Diagnostics",false);
  setupTile(form.openGuideButton,"help","User Guide",false);
  form.captureState->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
  form.captureState->setStyleSheet("background:#e2f3e8;color:#166534;border-radius:10px;padding:3px 12px;font-size:12px;font-weight:700;");
  for(const auto *item:{"Secure physical dongle","DDC device connectivity","Driver compatibility","Sufficient disk space","Record folder access"}) {
    auto row=new QHBoxLayout;row->setSpacing(10);
    auto badge=new QLabel(form.checksList);badge->setFixedSize(20,20);badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet("background:#146343;border-radius:4px;");
    auto pm=QIcon(":/icons/check.svg").pixmap(QSize(13,13),devicePixelRatioF());
    QPainter p(&pm);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(pm.rect(),Qt::white);p.end();
    badge->setPixmap(pm);
    auto text=new QLabel(item,form.checksList);text->setStyleSheet("font-size:14px;color:#172b3a;");
    row->addWidget(badge);row->addWidget(text,1);
    form.checksListLayout->addLayout(row);
  }
  const auto runtime=aero::inspectDdcRuntime(backend.captureConfig.runtime);
  form.sdk->setText(runtime.available ? "Runtime ready" : "Unavailable");form.sdk->setToolTip(QString::fromStdString(runtime.message));
  connect(form.exportDiagnosticsButton,&QPushButton::clicked,this,[this] { exportDiagnostics(); });
  connect(form.selfTestButton,&QPushButton::clicked,this,[this] { runSelfTest(); });
  connect(form.collectBundleButton,&QPushButton::clicked,this,[this]{collectDiagnostics();});
  connect(form.openLogsButton,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl::fromLocalFile(applicationLogDirectory()));});
  connect(form.openGuideButton,&QPushButton::clicked,this,[this]{QString path=QCoreApplication::applicationDirPath()+"/docs/Operator-Guide.html";if(!QFileInfo::exists(path))path=QString::fromUtf8(AEROTRACE_OPERATOR_GUIDE);if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))toast("Cannot open the operator guide: "+path);});
  form.releaseText->setText("Diagnostic run: "+applicationRunId()+"\nLogs: "+applicationLogDirectory()+"\nReceive-only hardware testing remains pending. Collect a support bundle after each bench test.");
  form.qt->setText(qVersion()); form.captureState->setText(backend.acquisition.active() ? "ACTIVE" : "IDLE");
  form.displayDrops->setText(QString::number(backend.acquisition.displayDrops()));
  form.os->setText(QSysInfo::prettyProductName()); form.architecture->setText(QSysInfo::currentCpuArchitecture());
#ifdef AEROTRACE_QWT
  form.plotBackend->setText("Qwt");
#else
  form.plotBackend->setText("Qt painter fallback");
#endif
}


void Window::runSelfTest(){
    QTemporaryDir dir;
    auto p =
        std::filesystem::path((dir.path() + "/selftest.atd").toStdWString());
    aero::Recorder w;
    bool ok = w.open(p);
    for (int i = 0; i < 100 && ok; ++i)
      ok = w.append(aero::synthetic(i));
    ok = w.close() && ok;
    auto r = aero::read(p);
    ok = ok && r.records.size() == 100 && !r.recovered;
    for (int i = 0; i < r.records.size(); ++i)
      ok = ok && (r.records[i] == aero::synthetic(i));
    QMessageBox::information(
        this, "Offline self test",
        ok ? "PASS: 100 synthetic records written, CRC-validated and replayed "
             "exactly.\nHardware and physical licensing were not tested."
           : "FAIL: recorder/replay self test failed.");
  }

void Window::exportDiagnostics(){
    auto p = QFileDialog::getSaveFileName(this, "Export diagnostics",
                                          sessionDir + "/diagnostics.json",
                                          "JSON (*.json)");
    if (p.isEmpty())
      return;
    QJsonObject o{{"version", QCoreApplication::applicationVersion()},
                  {"qt", qVersion()},
                  {"os", QSysInfo::prettyProductName()},
                  {"architecture", QSysInfo::currentCpuArchitecture()},
                  {"source", backend.captureConfig.hardware ? "DDC_HARDWARE" : "SIMULATED"},
                  {"capture_active", backend.acquisition.active()},
                  {"records", qint64(backend.archive.size())},
                  {"display_drops", qint64(backend.acquisition.displayDrops())},
                  {"hardware_qualified", false},
                  {"run_id",applicationRunId()},{"application_log",applicationLogPath()},{"logging_error",applicationLogError()},{"capture_path",currentFile},{"recording_directory",sessionDir}};
    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
      toast(f.errorString());
      return;
    }
    f.write(QJsonDocument(o).toJson());
    toast(f.commit() ? "Diagnostics exported" : "Diagnostics export failed");
  }
