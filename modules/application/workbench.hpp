#pragma once
#include "acquisition.hpp"
#include <utility>

namespace aero {
struct Series {
  std::vector<std::pair<double,double>> primary, secondary;
};
enum class ExportFormat { RawCsv, RawJson, EngineeringCsv };
struct ExportResult {
  std::string text, error;
  std::size_t count{};
};

// GUI-independent application state and operations. The UI reads this state on
// its owner thread; only Acquisition's internal worker touches acquisition I/O.
class Workbench {
public:
  Acquisition acquisition;
  std::vector<Record> archive, replayRecords;
  std::size_t replayIndex{}, replayWindowStart{};
  std::shared_ptr<RecordingIndex> recordingIndex;
  std::size_t replayCount() const { return recordingIndex?recordingIndex->size():replayRecords.size(); }
  Record replayAt(std::size_t i) const { return recordingIndex?recordingIndex->at(i):replayRecords.at(i); }
  std::uint64_t replayTimestamp(std::size_t i) const { return recordingIndex?recordingIndex->timestamp(i):replayRecords.at(i).timestampNs; }
  void useRecordingIndex(std::shared_ptr<RecordingIndex>);

  double replaySpeed{1}, replayPositionNs{};
  bool replayPlaying{}, replaySource{};
  Record selected{};
  Signal signal{"Sample Temperature", "degC"};
  CaptureConfig captureConfig;
  std::vector<Signal> profile;

  bool startRecording(const std::filesystem::path &, std::array<bool,4>);
  void stopRecording();
  bool accept(const std::vector<Record> &);
  ReadResult loadRecording(const std::filesystem::path &);
  bool seek(std::size_t);
  void advance(std::uint64_t elapsedMilliseconds);
  const std::vector<Record> &data(const std::vector<Record> &fallback) const;
  std::size_t end(const std::vector<Record> &fallback) const;
  std::optional<double> latest(const Signal &, const std::vector<Record> &) const;
  Series series(const std::string &role, unsigned signalIndex, const std::vector<Record> &,unsigned channel=0,int bus=-1,int rawColumn=-1) const;
  LiveValue liveValue(unsigned,const std::vector<Record> &,unsigned channel=0,int bus=-1) const;
  std::vector<LiveValue> liveValues(const std::vector<Record> &,unsigned channel=0,int bus=-1) const;
  static std::vector<Signal> demoSignals();
  std::vector<Signal> mappings() const;
  std::string applyProfile(std::vector<Signal>);
  ExportResult exportData(ExportFormat, std::uint64_t startMs, std::uint64_t endMs) const;
  static ExportResult exportRecords(const std::vector<Record> &, const Signal &,
      ExportFormat, std::uint64_t startMs, std::uint64_t endMs);
  static ExportResult exportProfileRecords(const std::vector<Record> &, const std::vector<Signal> &,
      ExportFormat, std::uint64_t startMs, std::uint64_t endMs);
};
}
