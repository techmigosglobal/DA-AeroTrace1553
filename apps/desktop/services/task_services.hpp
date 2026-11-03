#pragma once
#include "workbench.hpp"
#include <QJsonObject>
#include <QStringList>
#include <atomic>
namespace desktop {
QJsonArray checksToJson(const std::vector<aero::CheckRule> &);
QString checksFromJson(const QJsonArray &,const std::vector<aero::Signal> &,std::vector<aero::CheckRule> &);
QJsonObject simulationToJson(const aero::CaptureConfig &);
QString simulationFromJson(const QJsonObject &,aero::CaptureConfig &);
QString writeJson(const QString &,const QJsonObject &);
QString readJson(const QString &,QJsonObject &);
// One input at a time; 32 MiB per recording bounds the current in-memory reader.
QJsonObject batchAnalyze(const QStringList &,const QString &,const std::vector<aero::Signal> &,
  const std::vector<aero::CheckRule> &,std::atomic<bool> *cancel=nullptr,std::atomic<int> *progress=nullptr);
}
