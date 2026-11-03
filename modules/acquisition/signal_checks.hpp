#pragma once
#include "core.hpp"
#include <array>
#include <deque>

namespace aero {
struct CheckRule {
  std::string signalId;
  std::optional<double> minimum, maximum;
  std::uint64_t missingNs{};
  double hysteresis{};
};
struct CheckEvent {
  std::uint64_t timestampNs{}, recordId{};
  unsigned channel{}; int bus{};
  std::string signalId, kind, detail;
};
std::string validateChecks(const std::vector<CheckRule> &,const std::vector<Signal> &);
class SignalChecks {
  struct State {std::uint64_t last{};bool seen{},missing{},range{};};
  struct Stream {std::uint64_t now{},first{};bool seen{},protocol{},status{};};
  std::vector<Signal> mappings;
  std::vector<CheckRule> rules;
  std::vector<std::array<State,4>> states;
  std::array<Stream,4> streams{};
  std::vector<CheckEvent> pending;
  void event(std::uint64_t,unsigned,const Record *,const std::string &,const std::string &,const std::string &);
public:
  void configure(const std::vector<Signal> &,const std::vector<CheckRule> &);
  void consume(const Record &);
  void advance(unsigned stream,std::uint64_t cardNs);
  std::vector<CheckEvent> drain();
};
}

