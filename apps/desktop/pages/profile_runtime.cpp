#include "main_window.hpp"
#include "services/profile_io.hpp"
#include "services/manual_mapping.hpp"
#include "services/runtime_paths.hpp"
#include <QCryptographicHash>

namespace {
QString profilePath() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/active-profile.json";
}

QString deletionPath() {
  return desktop::dataDirectory() + "/pending-icd-deletion.json";
}

bool writeDeletion(const QJsonObject &record) {
  const auto path = deletionPath();
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QSaveFile file(path);
  const auto data = QJsonDocument(record).toJson(QJsonDocument::Compact);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

struct DeletionScope {
  QJsonObject data;
  QString kind = data["kind"].toString();
  QString identity = data["identity"].toString();
  QString owner = data["set"].toString();
  QString revision = data["revision"].toString();

  bool rule(const aero::Signal &signal) const {
    const auto eq = [](const std::string &value, const QString &text) { return QString::fromStdString(value) == text; };
    if (!owner.isEmpty() && !eq(signal.icdSetId, owner)) return false;
    if (!revision.isEmpty() && !eq(signal.icdRevision, revision)) return false;
    if (kind == "field") return eq(signal.id, identity);
    if (kind == "command") return eq(signal.messageId, identity);
    return signal.subsystem.empty() ? identity == "Unassigned" : eq(signal.subsystem, identity);
  }

  QJsonArray drafts(const QJsonArray &previous) const {
    QJsonArray result;
    for (const auto &value : previous) {
      auto command = value.toObject();
      const auto id = command["id"].toString();
      const auto fields = command["fields"].toArray();
      if (!owner.isEmpty() && owner != "manual") {
        result.append(command);
        continue;
      }
      if (kind == "subsystem" && command["subsystem"] == identity) continue;
      if (kind == "command" && id == identity) continue;
      if (kind == "field") {
        QJsonArray kept;
        for (const auto &field : fields)
          if ("manual/" + id + "/" + field.toObject()["id"].toString() != identity) kept.append(field);
        if (kept.size() != fields.size()) {
          if (kept.isEmpty()) continue;
          command["fields"] = kept;
        }
      }
      result.append(command);
    }
    return result;
  }
};
}  // namespace

void Window::updateEngineeringDetails() {
  if (page == 7 && recordedCsv) return;
  const auto &mappings = backend.mappings();
  const auto selectedStream = QString("CH%1 / %2").arg(analysisChannel).arg(analysisBus ? "B" : "A");
  if (auto current = scroll ? scroll->widget() : nullptr) {
    if (auto scope = current->findChild<QLabel *>("workspaceScope"))
      scope->setText((workspaceSubsystem.isEmpty() ? "All subsystems" : workspaceSubsystem) + " · " + selectedStream + " · " +
                     (backend.replaySource ? "Recorded profile" : "Live profile"));
    if (preferredSignalIndex < mappings.size() && inWorkspace(mappings[preferredSignalIndex]))
      if (auto route = current->findChild<QLabel *>("workspaceRoute")) {
        const auto &signal = mappings[preferredSignalIndex];
        const auto mappingStream = (signal.channel ? QString("CH%1").arg(signal.channel) : QString("Any channel")) + " / " +
                                   (signal.bus < 0 ? QString("Any bus") : QString(signal.bus ? "B" : "A"));
        route->setText(page == 7 && (plotRawColumn >= 0 || plots.empty() || plots.front()->property("signalIndex").toInt() < 0)
                           ? (plotRawColumn >= 0 ? "Raw words · " : "Select a signal · ") + selectedStream
                           : QString("Message %1 · RT %2 / SA %3 · %4\nMap: %5\nView: %6")
                                 .arg(QString::fromStdString(signal.messageId))
                                 .arg(signal.rt)
                                 .arg(signal.sa)
                                 .arg(signal.transmit ? "RT → BC" : "BC → RT", mappingStream, selectedStream));
      }
  }
  if (!engineeringDetails) return;
  if (preferredSignalIndex >= mappings.size() || !inWorkspace(mappings[preferredSignalIndex])) {
    engineeringDetails->setText("No published signal selected. Open Step 3 (Signal Mappings) to create and publish signals.");
    return;
  }
  const auto &signal = mappings[preferredSignalIndex];
  engineeringDetails->setText(QString("%1\nRT %2 / SA %3 · %4 · Word %5 (0-based) · %6\nEngineering = decoded × %7 + %8 %9")
                                  .arg(QString::fromStdString(signal.sourceSignalId.empty() ? signal.id : signal.sourceSignalId) + " · Message " + QString::fromStdString(signal.messageId))
                                  .arg(signal.rt)
                                  .arg(signal.sa)
                                  .arg(signal.transmit ? "RT → BC" : "BC → RT")
                                  .arg(signal.word)
                                  .arg(signal.leastSignificantWordFirst ? "LSW first" : "MSW first")
                                  .arg(desktop::displayValue(signal.scale))
                                  .arg(desktop::displayValue(signal.offset))
                                  .arg(QString::fromStdString(signal.unit)));
}

void Window::persistProfile() {
  QDir().mkpath(QFileInfo(profilePath()).absolutePath());
  QSaveFile output(profilePath());
  if (output.open(QIODevice::WriteOnly)) {
    auto profile = desktop::profileToJson(backend.profile);
    if (!manualRegistry.isEmpty()) profile["manual_library"] = manualRegistry;
    output.write(QJsonDocument(profile).toJson());
    if (!output.commit()) toast("Could not persist active profile");
  } else {
    toast("Could not persist active profile");
  }
}

QString Window::publishProfile(const std::vector<aero::Signal> &mappings, const QJsonObject *manual) {
  if (backend.acquisition.active() || (connecting && connectionIsRecording) || savingSession)
    return "Finish recording and saving before publishing mappings.";
  if (property("icdDeletePending").toBool() && !icdDeletionApplying)
    return "Finish the pending deletion before publishing.";
  if (property("icdArchivePending").toBool())
    return "An ICD archive or restore is still completing. Wait before publishing another profile.";
  if (!manual) {
    const auto manualRules = [](const auto &profile) {
      QMap<QString, QJsonObject> rules;
      for (const auto &signal : profile)
        if (signal.icdSetId == "manual") rules.insert(QString::fromStdString(signal.id), desktop::signalToJson(signal));
      return rules;
    };
    if (manualRules(mappings) != manualRules(backend.profile))
      return "Use Signal Mappings to change validated manual commands.";
  }
  if (auto error = aero::validateProfile(mappings); !error.empty()) return QString::fromStdString(error);
  if (auto error = aero::validateChecks(backend.captureConfig.checkRules, mappings); !error.empty())
    return QString::fromStdString(error);
  const auto path = profilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile output(path);
  auto next = desktop::profileToJson(mappings);
  const auto registry = manual ? *manual : manualRegistry;
  if (!registry.isEmpty()) next["manual_library"] = registry;
  const auto bytes = QJsonDocument(next).toJson();
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    return "Could not save active profile: " + output.errorString();
  auto previous = desktop::profileToJson(backend.profile);
  if (!manualRegistry.isEmpty()) previous["manual_library"] = manualRegistry;
  if (auto error = backend.applyProfile(mappings); !error.empty()) {
    QSaveFile restore(path);
    const auto old = QJsonDocument(previous).toJson();
    if (!restore.open(QIODevice::WriteOnly) || restore.write(old) != old.size() || !restore.commit())
      return "Publication failed and previous profile could not be restored: " + QString::fromStdString(error);
    return QString::fromStdString(error);
  }
  manualRegistry = registry;
  if (auto live = findChild<QDialog *>("liveEngineeringDialog")) live->close();
  refreshWorkflowStepper();
  applicationEvent("profile_activated", "",
                   {{"signals", int(mappings.size())},
                    {"profile_sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
  return {};
}

void Window::activateProfile(const QJsonObject &object) {
  std::vector<aero::Signal> mappings;
  if (auto error = desktop::profileFromJson(object, mappings); !error.isEmpty()) {
    toast(error);
    return;
  }
  if (auto error = publishProfile(mappings); !error.isEmpty()) {
    toast(error);
    return;
  }
  eventText = QString("Activated %1 validated signals").arg(backend.profile.size());
  if (unlocked) navigate(12);
  toast(eventText);
}

bool Window::icdDeletionAvailable() const {
  return unlocked && !backend.replaySource && !backend.acquisition.active() && !savingSession && !connecting && !closingCapture;
}

void Window::deleteIcdMapping(const QString &kind, const QString &identity, const QString &label, const QString &set, const QString &revision) {
  if (identity.isEmpty()) return;
  if (property("icdDeletePending").toBool()) {
    resumeIcdDeletion();
    return;
  }
  stageIcdDeletion({{"kind", kind}, {"identity", identity}, {"set", set}, {"revision", revision}}, label);
}

void Window::stageIcdDeletion(const QJsonObject &scope, const QString &label) {
  if (!icdDeletionAvailable()) {
    toast("Finish capture, save, and replay before deleting mappings.");
    return;
  }
  if (QFileInfo::exists(deletionPath())) {
    resumeIcdDeletion();
    return;
  }
  const DeletionScope selected{scope};
  const auto previous = desktop::profileToJson(backend.profile);
  const auto previousManual = manualRegistry;
  const auto previousDrafts = desktop::readManualDrafts();
  std::vector<aero::Signal> kept;
  for (const auto &signal : backend.profile)
    if (!selected.rule(signal)) kept.push_back(signal);
  auto manual = manualRegistry;
  auto drafts = previousDrafts;
  if (manual.contains("commands")) manual["commands"] = selected.drafts(manual["commands"].toArray());
  if (drafts.contains("commands")) drafts["commands"] = selected.drafts(drafts["commands"].toArray());
  const auto removed = backend.profile.size() - kept.size();
  if (!removed && manual == previousManual && drafts == previousDrafts) {
    toast("The selected mapping is already absent.");
    return;
  }
  if (auto error = aero::validateChecks(backend.captureConfig.checkRules, kept); !error.empty()) {
    toast("Update dependent signal checks before deleting: " + QString::fromStdString(error));
    return;
  }
  const QString impact = QString("Mapping: %1\nActive fields: %2").arg(selected.kind).arg(removed);
  if (QMessageBox::question(this, "Permanently delete?", QString("%1\n\n%2\nMatching drafts are removed. Saved recordings are retained.").arg(label, impact),
                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
    return;
  bool confirmed = false;
  if (QInputDialog::getText(this, "Confirm permanent deletion", "Type DELETE to confirm.", QLineEdit::Normal, {}, &confirmed) != "DELETE" || !confirmed)
    return;
  if (!icdDeletionAvailable() || desktop::profileToJson(backend.profile) != previous ||
      manualRegistry != previousManual || desktop::readManualDrafts() != previousDrafts) {
    toast("The workspace changed. Review the deletion scope again.");
    return;
  }
  auto record = scope;
  record["profile"] = desktop::profileToJson(kept);
  record["manual"] = manual;
  record["drafts"] = drafts;
  if (!writeDeletion(record)) {
    toast("Could not journal deletion.");
    return;
  }
  resumeIcdDeletion();
}

void Window::resumeIcdDeletion() {
  if (property("icdDeletionRunning").toBool()) return;
  QFile file(deletionPath());
  if (!file.exists()) return;
  setProperty("icdDeletePending", true);
  if (backend.acquisition.active() || savingSession || connecting) return;
  if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
    toast("Cannot read pending deletion.");
    return;
  }
  const auto record = QJsonDocument::fromJson(file.readAll()).object();
  file.close();
  std::vector<aero::Signal> signalsAfter;
  const auto error = desktop::profileFromJson(record["profile"].toObject(), signalsAfter);
  if (!error.isEmpty()) {
    toast("Pending deletion could not load its profile: " + error);
    return;
  }
  const auto registry = record["manual"].toObject();
  icdDeletionApplying = true;
  const auto published = publishProfile(signalsAfter, &registry);
  icdDeletionApplying = false;
  if (!published.isEmpty() || !desktop::saveManualDrafts(record["drafts"].toObject())) {
    toast(published.isEmpty() ? "Could not save draft changes." : published);
    return;
  }
  if (QFile::remove(deletionPath())) {
    setProperty("icdDeletePending", false);
    if (page == 12 || page == 6 || page == 7) navigate(page);
    toast("Deletion complete.");
  }
}
