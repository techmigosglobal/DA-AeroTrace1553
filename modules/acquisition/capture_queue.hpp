#pragma once

#include "source.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace aero {
inline std::size_t captureBatchBytes(const SourceBatch &batch) {
  std::size_t bytes=sizeof(SourceBatch)+batch.records.capacity()*sizeof(Record)+
    batch.packets.capacity()*sizeof(SourceBatch::Packet);
  for(const auto &record:batch.records)bytes+=record.words.capacity()*sizeof(std::uint16_t);
  for(const auto &packet:batch.packets)bytes+=packet.bytes.capacity();
  return bytes;
}

// A taken batch continues to count against the limit until processing completes.
class CaptureQueue {
public:
  struct Entry {
    std::shared_ptr<const SourceBatch> batch;
    std::size_t bytes{};
    std::chrono::steady_clock::time_point received;
    explicit operator bool()const{return bool(batch);}
  };
  enum class Result { Accepted, Full, Closed };

  explicit CaptureQueue(std::size_t capacity):limit(capacity){}
  Result push(Entry entry) {
    {
      std::lock_guard lock(mutex);
      if(closed)return Result::Closed;
      if(entry.bytes>limit-used)return Result::Full;
      const auto bytes=entry.bytes;
      queue.push_back(std::move(entry));
      used+=bytes;
      highWater=std::max(highWater,used);
    }
    ready.notify_one();
    return Result::Accepted;
  }
  Entry take(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex);
    ready.wait_for(lock,timeout,[this]{return closed || !queue.empty();});
    if(queue.empty())return {};
    auto entry=std::move(queue.front());
    queue.pop_front();
    return entry;
  }
  void release(std::size_t bytes) {
    std::lock_guard lock(mutex);
    used-=bytes;
  }
  void close() {
    {
      std::lock_guard lock(mutex);
      closed=true;
    }
    ready.notify_one();
  }
  bool finished() const {
    std::lock_guard lock(mutex);
    return closed && queue.empty();
  }
  std::pair<std::size_t,std::size_t> occupancy() const {
    std::lock_guard lock(mutex);
    return {used,highWater};
  }

private:
  const std::size_t limit;
  mutable std::mutex mutex;
  std::condition_variable ready;
  std::deque<Entry> queue;
  std::size_t used{},highWater{};
  bool closed{};
};
}
