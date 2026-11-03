#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <functional>
#include <mutex>

namespace aero {
struct Command {
  unsigned rt{}, sa{}, count{};
  bool transmit{}, mode{}, broadcast{};
};
Command command(std::uint16_t word);
struct Record {
  std::uint64_t id{}, timestampNs{};
  std::uint8_t channel{1}, bus{};
  std::uint16_t commandWord{}, statusWord{}, flags{};
  std::vector<std::uint16_t> words;
  // ATD2 provenance; original hardware Chapter 10 packets are retained separately.
  std::uint8_t source{}; // 0 = simulation, 1 = DDC MTI, 2 = DDC legacy MT
  std::uint16_t vendorStatus{}, commandWord2{}, statusWord2{}, validity{}, messageType{};
  std::uint64_t deviceTicks{}, hostMonotonicNs{};
  std::uint32_t tickResolutionNs{};
  bool operator==(const Record &) const = default;
};
struct Signal {
  std::string id, unit;
  unsigned rt{3}, sa{2}, word{}, lsb{}, width{16};
  bool signedValue{}, transmit{};
  double scale{0.01}, offset{};
  unsigned channel{}; // 0 matches either configured input channel
  int bus{-1};
  std::string numericType{"integer"}; // integer, float32, float64, bcd
  bool leastSignificantWordFirst{};
  std::string sourceDocument, sourceHash;
  unsigned sourcePage{};
  std::string operatorNote;
};
std::optional<double> decode(const Record &, const Signal &);
std::string validate(const Signal &);
std::string serializeProfile(const std::vector<Signal> &);
Record synthetic(std::uint64_t index);
std::uint32_t crc32(const std::vector<std::uint8_t> &);
class Recorder {
  std::ofstream out;
  std::vector<std::uint8_t> payloadBuffer,batchBuffer;

public:
  bool open(const std::filesystem::path &);
  bool append(const Record &);
  bool appendBatch(std::span<const Record>);
  bool flush();
  bool close();
  bool active() const { return out.is_open(); }
};
struct ReadResult {
  std::vector<Record> records;
  bool recovered{};
  std::string error;
};
ReadResult read(const std::filesystem::path &);
struct ScanResult {
  std::uint64_t count{};
  bool recovered{};
  std::string error;
};
// Validates each record before visiting it; memory use is independent of file size.
ScanResult scanRecording(const std::filesystem::path &,
    const std::function<bool(const Record &,std::uint64_t)> &);
ScanResult scanRecordingStream(std::istream &,const std::function<bool(const Record &,std::uint64_t)> &);
class RecordingIndex {
  struct Entry { std::uint64_t offset{}, timestamp{}, playback{}; };
  std::vector<Entry> entries;
  std::filesystem::path path;
  bool version2{};
  mutable std::ifstream input;
  mutable std::mutex inputMutex;
public:
  ScanResult open(const std::filesystem::path &,const std::function<void(const Record &)> &visitor={});
  std::size_t size() const { return entries.size(); }
  Record at(std::size_t) const;
  std::uint64_t timestamp(std::size_t i) const { return entries.at(i).timestamp; }
  std::uint64_t playbackTimestamp(std::size_t i) const { return entries.at(i).playback; }
};
} // namespace aero
