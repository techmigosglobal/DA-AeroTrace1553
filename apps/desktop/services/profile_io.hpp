#pragma once
#include "core.hpp"
#include <QJsonObject>
#include <QString>
namespace desktop {
QJsonObject signalToJson(const aero::Signal &);
QString signalFromJson(const QJsonObject &,aero::Signal &);
QJsonObject profileToJson(const std::vector<aero::Signal> &);
QString profileFromJson(const QJsonObject &,std::vector<aero::Signal> &);
}
