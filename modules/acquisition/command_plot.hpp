#pragma once

#include "downsample.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>

namespace aero {
struct CommandPlotSelection {
  std::string command;
  unsigned channel{1},bus{},first{},count{12};
  bool operator==(const CommandPlotSelection &) const=default;
};

struct CommandPlotFrame {
  CommandPlotSelection selection;
  std::vector<std::vector<Sample>> traces;
  double newest{};
};

class CommandTrace {
  static constexpr std::uint64_t period=100'000'000;
  static constexpr std::uint64_t buckets=600;
  struct Bucket {
    std::uint64_t index;
    std::optional<Sample> minimum,maximum;
    bool gap;
  };
  std::deque<Bucket> history;
  std::optional<std::uint64_t> previous;
public:
  void append(std::uint64_t timestamp,std::optional<double> value) {
    const auto index=timestamp/period;
    const bool discontinuity=previous && timestamp<*previous;
    if(discontinuity)history.clear();
    previous=timestamp;
    while(!history.empty() && index-history.front().index>=buckets)history.pop_front();
    if(history.empty() || history.back().index!=index) {
      const bool gap=discontinuity || (!history.empty() && index-history.back().index>1);
      history.push_back({index,{},{},gap});
    }
    auto &bucket=history.back();
    if(!value){bucket.gap=true;bucket.minimum.reset();bucket.maximum.reset();return;}
    const Sample sample(timestamp/1e9,*value);
    if(!bucket.minimum || *value<bucket.minimum->second)bucket.minimum=sample;
    if(!bucket.maximum || *value>=bucket.maximum->second)bucket.maximum=sample;
  }

  std::vector<Sample> points() const {
    std::vector<Sample> points;
    points.reserve(history.size()*3);
    for(const auto &bucket:history) {
      if(bucket.gap)points.emplace_back(bucket.index/10.,std::numeric_limits<double>::quiet_NaN());
      if(!bucket.minimum)continue;
      const auto &low=*bucket.minimum,&high=*bucket.maximum;
      if(low.first<=high.first) {
        points.push_back(low);
        if(high!=low)points.push_back(high);
      } else {points.push_back(high);points.push_back(low);}
    }
    return points;
  }
};
}
