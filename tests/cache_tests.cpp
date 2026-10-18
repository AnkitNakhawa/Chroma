#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "resource_cache.hpp"

int failures = 0;
#define CHECK(value) do { if (!(value)) { std::cerr << "line " << __LINE__ << ": " << #value << '\n'; ++failures; } } while (false)

int main() {
  using Cache = chroma::detail::ResourceCache<std::vector<int>>;
  auto weight = [](const std::vector<int>& value) { return value.capacity() * sizeof(int); };
  auto load = [] { return std::vector<int>(16, 7); };
  Cache sizing(std::numeric_limits<std::uint64_t>::max());
  static_cast<void>(sizing.get("a", load, weight));
  const auto bytes = sizing.usage().resident_bytes;
  CHECK(bytes >= 16 * sizeof(int));
  Cache cache(bytes * 2);
  auto a = cache.get("a", load, weight);
  auto b = cache.get("b", load, weight);
  CHECK(cache.usage().entries == 2);
  CHECK(cache.get("a", []() -> std::vector<int> { throw std::runtime_error("must hit"); }, weight) == a);
  static_cast<void>(cache.get("c", load, weight)); // b is least recently used.
  CHECK(cache.usage().evictions == 1);
  CHECK(cache.usage().resident_bytes == bytes * 2);
  CHECK((*b)[0] == 7); // An outstanding caller handle survives eviction.
  CHECK(cache.get("a", load, weight) == a);
  static_cast<void>(cache.get("b", load, weight));
  CHECK(cache.usage().misses == 4 && cache.usage().hits == 2);
  CHECK(cache.usage().insertions == 4 && cache.usage().evictions == 2);
  const auto before = cache.usage();
  bool rejected = false;
  try { static_cast<void>(cache.get("bad", []() -> std::vector<int> { throw std::runtime_error("failed"); }, weight)); }
  catch (const std::runtime_error&) { rejected = true; }
  CHECK(rejected);
  CHECK(cache.usage().resident_bytes == before.resident_bytes);
  CHECK(cache.usage().evictions == before.evictions);
  CHECK(cache.usage().load_failures == 1);
  static_cast<void>(cache.get("large", [] { return std::vector<int>(10000); }, weight));
  CHECK(cache.usage().bypasses == 1 && cache.usage().entries == 2);
  CHECK(cache.usage().evictions == before.evictions);
  cache.set_budget(bytes);
  CHECK(cache.usage().entries == 1 && cache.usage().resident_bytes == bytes);
  cache.set_budget(0);
  CHECK(cache.usage().entries == 0 && cache.usage().resident_bytes == 0);
  static_cast<void>(cache.get("a", load, weight));
  CHECK(cache.usage().bypasses == 2 && cache.usage().entries == 0);
  cache.set_budget(bytes);
  static_cast<void>(cache.get("a", load, weight));
  CHECK(cache.usage().entries == 1);
  cache.clear();
  CHECK(cache.usage().resident_bytes == 0 && cache.usage().entries == 0);
  CHECK(cache.usage().peak_resident_bytes == bytes * 2);
  // Keys have real storage cost: empty/zero-payload values cannot grow without bound.
  chroma::detail::ResourceCache<int> empty(1);
  static_cast<void>(empty.get("empty", [] { return 0; }, [](int) { return 0; }));
  CHECK(empty.usage().bypasses == 1 && empty.usage().entries == 0);
  // Repeated insert/evict/re-hash exercises borrowed-key lifetime and accounting.
  for (int index = 0; index < 1000; ++index) {
    static_cast<void>(cache.get(std::to_string(index), load, weight));
    CHECK(cache.usage().resident_bytes <= cache.usage().budget_bytes);
    CHECK(cache.usage().entries <= 1);
  }
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
