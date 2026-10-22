#pragma once
#include <cstddef>
#include <memory>
#include <type_traits>

namespace chroma::detail {
bool serial_execution(bool enabled) noexcept;
class SerialScope {
 public:
  explicit SerialScope(bool enabled) noexcept;
  ~SerialScope() { serial_execution(previous_); }
  SerialScope(const SerialScope&) = delete;
  SerialScope& operator=(const SerialScope&) = delete;
 private:
  bool previous_;
};
using RangeTask = void (*)(void*, std::size_t, std::size_t);
// Synchronous: no task or captured data survives this call. A busy pool falls
// back inline; nested use never waits for the pool's own workers.
void parallel_ranges(std::size_t count, std::size_t grain, void* context, RangeTask task);
[[nodiscard]] std::size_t cpu_thread_limit() noexcept;

template<class Function>
void parallel_for(std::size_t count, std::size_t grain, Function&& function) {
  using Callable = std::remove_reference_t<Function>;
  parallel_ranges(count, grain, const_cast<void*>(static_cast<const void*>(std::addressof(function))), [](void* context, std::size_t begin, std::size_t end) {
    (*static_cast<Callable*>(context))(begin, end);
  });
}
}
