#pragma once
#include <algorithm>
#include <utility>
#include <vector>
namespace aero {
using Sample=std::pair<double,double>;
// Min/max envelope per time-order bucket. Preserve endpoints and narrow spikes;
// raw recordings and decoded samples are never replaced by these display points.
inline std::vector<Sample> downsample(const std::vector<Sample> &input,std::size_t limit=1024) {
  if(input.size()<=limit) return input;
  if(limit<4) return limit ? std::vector<Sample>{input.front()} : std::vector<Sample>{};
  std::vector<Sample> output;output.reserve(limit);output.push_back(input.front());
  const auto buckets=(limit-2)/2;
  for(std::size_t bucket=0;bucket<buckets;++bucket) {
    const auto begin=1+(input.size()-2)*bucket/buckets;
    const auto end=1+(input.size()-2)*(bucket+1)/buckets;
    if(begin>=end) continue;
    auto low=begin,high=begin;
    for(auto index=begin+1;index<end;++index) {
      if(input[index].second<input[low].second) low=index;
      if(input[index].second>input[high].second) high=index;
    }
    if(low>high) std::swap(low,high);
    output.push_back(input[low]);if(high!=low) output.push_back(input[high]);
  }
  output.push_back(input.back());return output;
}
}
