#pragma once
#include <QtCore>
bool initializeApplicationLogging(const QString &directory={});
bool applicationEvent(const QString &event,const QString &detail={},const QJsonObject &fields={},QtMsgType severity=QtInfoMsg);
void installApplicationQtLogging();
QString applicationLogDirectory();
QString applicationLogPath();
QString applicationRunId();
QString applicationLogError();
bool flushApplicationLogging();
void shutdownApplicationLogging();
void installApplicationCrashHandler();
void writeApplicationCrashDump(void *exceptionPointers=nullptr) noexcept;
