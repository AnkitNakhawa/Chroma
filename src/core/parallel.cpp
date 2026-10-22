#include "parallel.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string_view>
#include <thread>

namespace chroma::detail {
namespace { thread_local bool force_serial = false; }
SerialScope::SerialScope(bool enabled) noexcept : previous_(force_serial) { force_serial=force_serial || enabled; }
bool serial_execution(bool enabled) noexcept {
  const auto previous=force_serial;
  force_serial=enabled;
  return previous;
}
std::size_t cpu_thread_limit() noexcept {
  static const auto limit = [] {
    const auto hardware = std::max(1U, std::thread::hardware_concurrency());
    const auto maximum = std::min(4U, hardware);
    if (const char* input = std::getenv("CHROMA_CPU_THREADS")) {
      unsigned value = 0;
      const std::string_view text(input);
      const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data()+text.size() || value < 1 || value > 4)
        return std::size_t{1};
      return static_cast<std::size_t>(std::min(value, maximum));
    }
    return static_cast<std::size_t>(maximum);
  }();
  return limit;
}

namespace {
thread_local bool executing_job = false;
class Pool {
 public:
  explicit Pool(std::size_t limit) : limit_(limit) {
    try {
      for (std::size_t i=1; i<limit_; ++i) workers_[i-1] = std::thread([this,i] { worker(i); });
    } catch (...) {
      stop(); // Never destroy a joinable thread after partial startup failure.
      throw;
    }
  }
  ~Pool() { stop(); }
  bool run(std::size_t count, std::size_t participants, void* context, RangeTask task) {
    std::unique_lock dispatch(dispatch_, std::try_to_lock);
    if (!dispatch.owns_lock()) return false;
    {
      std::lock_guard lock(state_);
      count_=count; participants_=participants; context_=context; task_=task;
      pending_=participants-1; error_=nullptr; ++generation_;
    }
    ready_.notify_all();
    invoke(0, count, participants, context, task);
    std::unique_lock lock(state_);
    done_.wait(lock, [this] { return pending_==0; });
    auto error=error_;
    context_=nullptr; task_=nullptr;
    lock.unlock();
    if (error) std::rethrow_exception(error);
    return true;
  }
 private:
  void stop() noexcept {
    { std::lock_guard lock(state_); stopping_=true; }
    ready_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
  }
  void invoke(std::size_t index, std::size_t count, std::size_t participants, void* context, RangeTask task) {
    const auto block=count/participants, remainder=count%participants;
    const auto begin=index*block+std::min(index,remainder);
    const auto end=begin+block+(index<remainder ? 1 : 0);
    struct Scope {
      bool previous{executing_job};
      Scope() { executing_job=true; }
      ~Scope() { executing_job=previous; }
    } scope;
    try { task(context,begin,end); }
    catch (...) { std::lock_guard lock(state_); if (!error_) error_=std::current_exception(); }
  }
  void worker(std::size_t index) {
    std::size_t observed=0;
    std::unique_lock lock(state_);
    for (;;) {
      ready_.wait(lock,[&] { return stopping_ || generation_!=observed; });
      if (stopping_) return;
      observed=generation_;
      if (index>=participants_) continue;
      const auto count=count_, participants=participants_;
      auto* context=context_; const auto task=task_;
      lock.unlock(); invoke(index,count,participants,context,task); lock.lock();
      if (--pending_==0) done_.notify_one();
    }
  }
  std::size_t limit_;
  std::array<std::thread,3> workers_;
  std::mutex dispatch_,state_;
  std::condition_variable ready_,done_;
  bool stopping_{false};
  std::size_t generation_{0},count_{0},participants_{0},pending_{0};
  void* context_{nullptr}; RangeTask task_{nullptr}; std::exception_ptr error_;
};
Pool* pool() noexcept {
  static const auto instance=[]() noexcept -> std::unique_ptr<Pool> {
    try { return std::make_unique<Pool>(cpu_thread_limit()); }
    catch (...) { return nullptr; } // Resource exhaustion preserves serial rendering.
  }();
  return instance.get();
}
}

bool serial_requested() noexcept { return force_serial || executing_job; }

void parallel_ranges(std::size_t count, std::size_t grain, void* context, RangeTask task) {
  if (count==0) return;
  if (executing_job || force_serial) { task(context,0,count); return; }
  const auto participants=std::min(cpu_thread_limit(),count/std::max(std::size_t{1},grain));
  if (participants<2) { task(context,0,count); return; }
  auto* executor=pool();
  if (!executor || !executor->run(count,participants,context,task)) task(context,0,count);
}
}
