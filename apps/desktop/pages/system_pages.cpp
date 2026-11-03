#include "main_window.hpp"
#include "services/key_gate.hpp"
#include "services/runtime_paths.hpp"
#include "ui_security_page.h"
#include "ui_diagnostics_page.h"
#include "ui_preferences_page.h"

void Window::security() {
  auto form=showForm<Ui::SecurityPage>();
  form.status->setText("Not verified");form.operation->setText("Local workspace access");form.entitlement->setText("License adapter not configured");
  if(desktop::keyRequired()){form.status->setText(applicationKeyVerified?"PIV key verified":"Key required");form.operation->setText("Fresh key proof every 5 seconds; locks within 10 seconds without proof");form.entitlement->setText(applicationKeyMessage);}else form.entitlement->setText("Application key enforcement disabled in this build; enrollment pending");
  form.monitor->setText("Receive-only source selection");form.decoder->setText("Validated ICD profiles");form.replay->setText("Local recordings");form.exports->setText("Local datasets");
  form.removeKeyButton->setText("Lock workspace and stop capture");
  connect(form.removeKeyButton,&QPushButton::clicked,this,[this] { removeKey(); });
}


void Window::removeKey(){
    if(savingSession){toast("Wait for Stop & save to finish before locking the workspace.");return;}
    automaticHeld=true;
    backend.stopRecording();
    backend.replayPlaying = false;
    unlocked = false;
    licenseBadge->setText("● LOCKED");
    footerBadge->setText("● Missing");
    stateBadge->setText("○ STANDBY");
    eventText = "Workspace locked: capture stopped and file closed";
    navigate(0);
    toast(eventText);
  }

void Window::preferences() {
    auto form = showForm<Ui::PreferencesPage>();
    connect(form.restoreButton, &QPushButton::clicked, this, [this] { settings.clear(); navigate(16); });
    connect(form.saveButton, &QPushButton::clicked, this, [this] { savePreferences(); });
    preferenceChecks = {form.autoScrollCheck, form.denseCheck, form.reduceMotionCheck};
    const QStringList keys = {"autoscroll", "dense", "reduceMotion"};
    for (int i = 0; i < preferenceChecks.size(); ++i) {
      preferenceChecks[i]->setProperty("key", keys[i]);
      preferenceChecks[i]->setChecked(settings.value(keys[i], i == 0).toBool());
    }
    auto layoutRow=new QHBoxLayout;layoutRow->addWidget(new QLabel("Monitor layout"));
    monitorLayoutEdit=new QComboBox;monitorLayoutEdit->setObjectName("monitorLayoutSetting");monitorLayoutEdit->addItem("Movable workspace windows","windows");monitorLayoutEdit->addItem("Tabs","tabs");monitorLayoutEdit->setCurrentIndex(settings.value("monitorLayout","windows").toString()=="tabs"?1:0);layoutRow->addWidget(monitorLayoutEdit,1);pageLayout->addLayout(layoutRow);
    fpsEdit = form.fpsEdit;
    fpsEdit->setValue(settings.value("fps", 20).toInt());
    recordFolder = form.recordFolder;
    recordFolder->setText(sessionDir);
    exportFolder = form.exportFolder;
    exportFolder->setText(settings.value("exportFolder", desktop::defaultExportDirectory()).toString());
}

void Window::savePreferences(){
    auto dir = recordFolder->text().trimmed();
    if (!QDir().mkpath(dir)) {
      toast("Cannot create recording directory.");
      return;
    }
    auto csvDir=exportFolder->text().trimmed();
    if(csvDir.isEmpty()||!QDir().mkpath(csvDir)){toast("Choose a writable CSV output folder.");return;}
    sessionDir = QDir(dir).absolutePath();
    settings.setValue("recordFolder", sessionDir);
    settings.setValue("exportFolder", QDir(csvDir).absolutePath());
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
  auto rescan=new QPushButton("Rescan DDC devices / drivers");auto manager=new QPushButton("Open DDC Card Manager");
  pageLayout->addWidget(rescan,0,Qt::AlignLeft);pageLayout->addWidget(manager,0,Qt::AlignLeft);
  connect(rescan,&QPushButton::clicked,this,[this]{rescanDdc();});connect(manager,&QPushButton::clicked,this,&Window::openCardManager);
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
                  {"physical_license_verified", applicationKeyVerified}, {"application_key_required",desktop::keyRequired()},
                  {"run_id",applicationRunId()},{"application_log",applicationLogPath()},{"logging_error",applicationLogError()},{"capture_path",currentFile},{"recording_directory",sessionDir}};
    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
      toast(f.errorString());
      return;
    }
    f.write(QJsonDocument(o).toJson());
    toast(f.commit() ? "Diagnostics exported" : "Diagnostics export failed");
  }
