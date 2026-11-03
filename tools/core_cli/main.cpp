#include "core.hpp"
#include <charconv>
#include <iostream>
int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: aerotrace_core_cli generate <file.atd> [count]\n      "
                 " aerotrace_core_cli verify|replay|decode <file.atd>\n";
    return 2;
  }
  std::string action = argv[1];
  std::filesystem::path path = argv[2];
  if (action == "generate") {
    unsigned count = 1000;
    if (argc > 3) {
      auto end = argv[3] + std::char_traits<char>::length(argv[3]);
      auto parsed = std::from_chars(argv[3], end, count);
      if (parsed.ec != std::errc{} || parsed.ptr != end || count > 1000000) {
        std::cerr << "Count must be 0–1000000\n";
        return 2;
      }
    }
    aero::Recorder writer;
    if (!writer.open(path)) {
      std::cerr << "Cannot create recording\n";
      return 1;
    }
    for (unsigned i = 0; i < count; ++i)
      if (!writer.append(aero::synthetic(i))) {
        std::cerr << "Write failed\n";
        return 1;
      }
    if (!writer.close())
      return 1;
    std::cout << "Generated " << count << " SIMULATED records\n";
    return 0;
  }
  if (action != "verify" && action != "replay" && action != "decode") {
    std::cerr << "Unknown action\n";
    return 2;
  }
  auto result = aero::read(path);
  if (!result.error.empty()) {
    std::cerr << result.error << '\n';
    return 1;
  }
  if (action == "verify") {
    std::cout << result.records.size()
              << " CRC-valid records; recovered=" << result.recovered << '\n';
    return result.recovered ? 3 : 0;
  }
  aero::Signal signal{"TEMP_DEMO", "degC"};
  for (auto &r : result.records) {
    auto c = aero::command(r.commandWord);
    if (action == "decode") {
      auto value = aero::decode(r, signal);
      std::cout << r.id << ',' << r.timestampNs << ',';
      if (value)
        std::cout << *value << ",VALID";
      else
        std::cout << ",NO_MATCH";
      std::cout << '\n';
    } else {
      std::cout << r.id << ',' << r.timestampNs << ',' << int(r.channel) << ','
                << (r.bus ? 'B' : 'A') << ',' << c.rt << ',' << c.sa << ','
                << std::hex << r.commandWord;
      for (auto w : r.words)
        std::cout << ' ' << w;
      std::cout << std::dec << '\n';
    }
  }
  return result.recovered ? 3 : 0;
}
