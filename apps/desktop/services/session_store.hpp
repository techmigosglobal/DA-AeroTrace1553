#pragma once
#include <QtCore>
#include "source.hpp"
namespace desktop {
QString describeSession(const QString &,const aero::CaptureConfig &);
struct SessionSaveResult { QString error,manifest,runName;quint64 run{},external{},internal{};bool recovered{},incomplete{}; };
QString sessionDisplayName(const QString &recording);
QString validateRunName(const QString &);
QString suggestedRunName(const QString &directory);
SessionSaveResult exportSession(const QString &recording,const QString &directory,const QString &captureError={},const QString &runName={});
QString renameRecording(const QString &source,const QString &destination);
}
