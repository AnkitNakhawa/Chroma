#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "chroma/core/cache.hpp"

namespace chroma::detail {

inline std::uint64_t add_cache_bytes(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    throw std::overflow_error("prepared resource size exceeds cache accounting range");
  return left + right;
}

// Strings are owned only by stable list nodes; the index borrows their storage.
// Live callers own shared handles, so eviction never invalidates a resource in use.
// Retained-byte limits exclude temporary preparation and outstanding handles.
template <typename Value>
class ResourceCache {
 public:
  explicit ResourceCache(std::uint64_t budget) { usage_.budget_bytes = budget; }
  ResourceCache(const ResourceCache&) = delete;
  ResourceCache& operator=(const ResourceCache&) = delete;

  template <typename Load, typename PayloadBytes>
  std::shared_ptr<const Value> get(std::string key, Load load, PayloadBytes payload_bytes) {
    if (const auto found = index_.find(key); found != index_.end()) {
      ++usage_.hits;
      entries_.splice(entries_.begin(), entries_, found->second);
      return found->second->value;
    }
    ++usage_.misses;
    std::shared_ptr<const Value> value;
    std::uint64_t bytes;
    try {
      value = std::make_shared<Value>(load());
      bytes = add_cache_bytes(sizeof(Entry) + sizeof(Value) + 2 * sizeof(void*),
                              add_cache_bytes(key.capacity() + 1, payload_bytes(*value)));
    } catch (...) { ++usage_.load_failures; throw; }
    if (usage_.budget_bytes == 0 || bytes > usage_.budget_bytes) {
      ++usage_.bypasses;
      return value;
    }
    while (usage_.resident_bytes > usage_.budget_bytes - bytes) evict_one();
    entries_.push_front(Entry{std::move(key), value, bytes});
    try { index_.emplace(entries_.front().key, entries_.begin()); }
    catch (...) { entries_.pop_front(); throw; }
    usage_.resident_bytes += bytes;
    usage_.peak_resident_bytes = std::max(usage_.peak_resident_bytes, usage_.resident_bytes);
    ++usage_.entries;
    ++usage_.insertions;
    return value;
  }

  void set_budget(std::uint64_t budget) {
    const auto previous_budget = usage_.budget_bytes;
    usage_.budget_bytes = budget;
    while (usage_.resident_bytes > budget) evict_one();
    if (budget == 0) release_index();
    else if (budget < previous_budget) {
      // Reclaim the old hash table as well as resource storage after a shrink.
      Index compact;
      compact.reserve(entries_.size());
      for (auto entry = entries_.begin(); entry != entries_.end(); ++entry)
        compact.emplace(entry->key, entry);
      index_.swap(compact);
    }
  }
  void clear() {
    while (!entries_.empty()) evict_one();
    release_index();
  }
  [[nodiscard]] CacheUsage usage() const noexcept { return usage_; }

 private:
  struct Entry {
    std::string key;
    std::shared_ptr<const Value> value;
    std::uint64_t bytes;
  };
  using Entries = std::list<Entry>;
  using Index = std::unordered_map<std::string_view, typename Entries::iterator>;
  void evict_one() {
    const auto& entry = entries_.back();
    index_.erase(entry.key);
    usage_.resident_bytes -= entry.bytes;
    --usage_.entries;
    ++usage_.evictions;
    entries_.pop_back();
  }
  void release_index() { Index{}.swap(index_); }
  Entries entries_;
  Index index_;
  CacheUsage usage_;
};

}  // namespace chroma::detail
