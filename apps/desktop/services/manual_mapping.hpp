#pragma once

#include "core.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

namespace desktop {
struct ManualMappingResult {
  std::vector<aero::Signal> rules;
  QStringList fields;
  QString error;
};

QJsonObject readManualDrafts();
bool saveManualDrafts(const QJsonObject &);
// Migrates legacy 1-based draft/registry commands to 0-based word indices once.
QJsonArray normalizeManualCommands(const QJsonArray &commands);
QString manualRouteLabel(const QJsonObject &command);
ManualMappingResult checkManualMapping(const QJsonObject &command);
// Returns true when both mappings can consume the same message frame on the same stream.
bool overlappingManualRoute(const aero::Signal &a,const aero::Signal &b);
}
