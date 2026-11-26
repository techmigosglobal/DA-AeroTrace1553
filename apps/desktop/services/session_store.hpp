#pragma once
#include <QtCore>
#include "source.hpp"
#include "record_csv.hpp"
namespace desktop {
bool verifiedOfflineFixture(const QString &recording,QString *source=nullptr);
QString describeSession(const QString &,const aero::CaptureConfig &,const QString &runName={},aero::CsvExportOptions={});
struct SessionSaveResult { QString error,manifest,runName;quint64 run{},external{},internal{};bool recovered{},incomplete{}; };
enum class SessionExportIntent { Retry, SelectedFormat };
enum class SessionSavePhase { Writing, Verifying };
struct SessionExportTarget {QString name,directory,descriptor;};
SessionExportTarget sessionExportTarget(const QString &recording,const QString &directory,const QString &runName,
                                        aero::CsvExportOptions options,SessionExportIntent intent);
struct SessionCsvSource {QString path,sha256;unsigned channel{};aero::CsvExportOptions options;};
QList<SessionCsvSource> sessionCsvSources(const QString &recording);
QStringList recoverableSessions(const QStringList &roots);
QString sessionDisplayName(const QString &recording);
QString validateRunName(const QString &);
QString suggestedRunName(const QString &directory);
SessionSaveResult exportSession(const QString &recording,const QString &directory,const QString &captureError={},const QString &runName={},aero::CsvExportOptions options={},
                                SessionExportIntent intent=SessionExportIntent::Retry,
                                const std::function<void(SessionSavePhase)> &progress={},QString expectedName={});
QString renameRecording(const QString &source,const QString &destination);
}
