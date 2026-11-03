#pragma once
#include <QtCore>
namespace desktop {
bool ddcAssignmentMatchesDevice(const QString &assignmentId,const QString &instanceId);
QJsonObject classifyDdcAssignment(int logical,const QJsonObject &values);
QString ddcDriverBinding(const QString &ids,const QString &service,const QString &inf,int problem,const QString &status);
QJsonObject discoverDdc(const QString &runtime);
QString discoverySummary(const QJsonObject &);
}
