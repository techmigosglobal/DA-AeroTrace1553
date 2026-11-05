#pragma once
#include <QtCore>

namespace desktop {
struct CardManagerLocation { QString path,error; QStringList examined; };
struct CardManagerLaunch { bool launched{},cancelled{}; quint32 errorCode{},processId{}; QString message; };
// The candidate resolver is separate from registry discovery and process launch
// so tests never need to elevate or execute vendor code.
CardManagerLocation resolveCardManager(const QStringList &candidates,const QString &explicitPath={});
CardManagerLocation locateCardManager(const QString &explicitPath={});
CardManagerLaunch launchCardManager(const QString &path);
}
