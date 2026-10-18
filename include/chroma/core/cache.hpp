#pragma once

#include <cstdint>

namespace chroma {

struct CpuCacheConfig {
  std::uint64_t image_budget_bytes{64ULL * 1024 * 1024};
  std::uint64_t text_budget_bytes{16ULL * 1024 * 1024};
};

// Accounted retained resource/key storage, not total process/allocator memory.
// Counters are cumulative for the context lifetime. Evictions include explicit
// clears and budget reductions. A failed preparation counts as a miss and failure.
struct CacheUsage {
  std::uint64_t budget_bytes{0};
  std::uint64_t resident_bytes{0};
  std::uint64_t peak_resident_bytes{0};
  std::uint64_t entries{0};
  std::uint64_t hits{0};
  std::uint64_t misses{0};
  std::uint64_t insertions{0};
  std::uint64_t evictions{0};
  std::uint64_t bypasses{0};
  std::uint64_t load_failures{0};

  friend bool operator==(const CacheUsage&, const CacheUsage&) = default;
};

struct CpuCacheUsage {
  CacheUsage images;
  CacheUsage text;
  friend bool operator==(const CpuCacheUsage&, const CpuCacheUsage&) = default;
};

}  // namespace chroma
