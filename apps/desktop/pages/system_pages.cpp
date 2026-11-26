#include "main_window.hpp"
#include "services/operator_style.hpp"
#include "services/runtime_paths.hpp"
#include "ui_diagnostics_page.h"
#include "ui_preferences_page.h"

void Window::preferences() {
    auto form = showForm<Ui::PreferencesPage>();
    form.interfaceLayout->insertWidget(0,desktop::sectionHeading("Interface Preferences","device"));
    form.captureCardLayout->insertWidget(0,desktop::sectionHeading("Capture & Plotting","activity",true));
    form.safetyLayout->insertWidget(0,desktop::sectionHeading("Advanced Safety","shield"));
    form.pathsLayout->insertWidget(0,desktop::sectionHeading("Session Paths","folder",true));
    const bool spacious=height()>=850;
    form.preferencesGrid->setRowStretch(1,3);
    form.preferencesGrid->setRowStretch(2,2);
    form.preferencesGrid->setContentsMargins(0,0,0,spacious?52:0);
    form.interfaceGroup->setMinimumHeight(spacious?324:232);
    form.safetyGroup->setMinimumHeight(spacious?186:132);
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
    auto layoutRow=new QHBoxLayout;layoutRow->addWidget(new QLabel("Monitor layout"));
    monitorLayoutEdit=new QComboBox;monitorLayoutEdit->setObjectName("monitorLayoutSetting");monitorLayoutEdit->addItem("Movable workspace windows","windows");monitorLayoutEdit->addItem("Tabs","tabs");monitorLayoutEdit->setCurrentIndex(settings.value("monitorLayout","windows").toString()=="tabs"?1:0);layoutRow->addWidget(monitorLayoutEdit,1);form.captureCardLayout->addLayout(layoutRow);
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
  auto rescan=new QPushButton("Rescan DDC devices / drivers",content);auto manager=new QPushButton("Open DDC Card Manager",content);
  desktop::actionIcon(rescan,"search");
  desktop::actionIcon(manager,"device");
  if(titleActions){titleActions->addWidget(rescan);titleActions->addWidget(manager);}
  auto guideCard=new QFrame(content);
  guideCard->setProperty("card",true);
  auto guideLayout=new QVBoxLayout(guideCard);
  guideLayout->setContentsMargins(18,14,18,14);
  guideLayout->setSpacing(6);
  auto guideHeading=new QLabel("5-Step Guided Workflow",guideCard);
  guideHeading->setProperty("sectionTitle",true);
  auto guideText=new QLabel(
    "Step 1 · Device Setup: Select DDC hardware or offline simulator, channels, and recording folder.\n"
    "Step 2 · Monitor & Record: Inspect frames, switch between RT Statistics / Message Routes / Chronological / RT Matrix 00–31, click Details or Map in ICD →.\n"
    "Step 3 · Signal Mappings: Define RT/SA/Tx commands and 0-based Word 0–31 fields manually or import JSON/CSV.\n"
    "Step 4 · Engineering & Plots: Inspect live converted engineering values, preview raw words, or analyze Qwt charts.\n"
    "Step 5 · Sessions & Exports: Browse/replay saved .atd files, run streaming CSV Viewer, and export time-bounded datasets.",
    guideCard);
  guideText->setWordWrap(true);
  guideLayout->addWidget(guideHeading);
  guideLayout->addWidget(guideText);
  pageLayout->insertWidget(1,guideCard);
  desktop::actionIcon(form.exportDiagnosticsButton,"download");
  desktop::actionIcon(form.selfTestButton,"activity");
  desktop::actionIcon(form.collectBundleButton,"folder");
  desktop::actionIcon(form.openLogsButton,"file");
  desktop::actionIcon(form.openGuideButton,"help");
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
                  {"run_id",applicationRunId()},{"application_log",applicationLogPath()},{"logging_error",applicationLogError()},{"capture_path",currentFile},{"recording_directory",sessionDir}};
    QSaveFile f(p);
    if (!f.open(QIODevice::WriteOnly)) {
      toast(f.errorString());
      return;
    }
    f.write(QJsonDocument(o).toJson());
    toast(f.commit() ? "Diagnostics exported" : "Diagnostics export failed");
  }
