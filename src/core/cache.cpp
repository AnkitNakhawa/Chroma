#include "chroma/core/cache.hpp"

#include <nlohmann/json.hpp>

namespace chroma {

std::string cpu_cache_usage_json(const CpuCacheUsage& usage) {
  const auto entry = [](const CacheUsage& value) {
    return nlohmann::json{{"budget_bytes", value.budget_bytes},
      {"resident_bytes", value.resident_bytes}, {"peak_resident_bytes", value.peak_resident_bytes},
      {"entries", value.entries}, {"hits", value.hits}, {"misses", value.misses},
      {"insertions", value.insertions}, {"evictions", value.evictions},
      {"bypasses", value.bypasses}, {"load_failures", value.load_failures}};
  };
  return nlohmann::json{{"policy", "lru"}, {"accounting", "retained_resource_bytes"},
                        {"images", entry(usage.images)}, {"text", entry(usage.text)}}.dump(2);
}

}  // namespace chroma
