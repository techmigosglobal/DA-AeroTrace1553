#pragma once
#include <QtCore>
#include <array>
namespace desktop {
bool ddcAssignmentMatchesDevice(const QString &assignmentId,const QString &instanceId);
QJsonObject classifyDdcAssignment(int logical,const QJsonObject &values);
QJsonArray ddcAssignmentChoices(const QJsonObject &report);
QJsonObject resolveDdcAssignments(const QJsonObject &report,const std::array<bool,4> &enabled,
                                 const std::array<int,2> &saved,const std::array<bool,2> &hasSaved,bool manual=false);
QString ddcDriverBinding(const QString &ids,const QString &service,const QString &inf,int problem,const QString &status);
QJsonObject discoverDdc(const QString &runtime,bool probeRuntime=true);
QString runtimeReadiness(const QJsonObject &);
QString discoverySummary(const QJsonObject &);
}
