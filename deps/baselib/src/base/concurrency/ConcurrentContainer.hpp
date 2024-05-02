#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>

namespace base {

template <typename T>
class ConcurrentContainer {
  std::mutex mutex;
  std::condition_variable cv;
  bool exit = false;

  T container{};

 public:
  void request_exit() {
    std::lock_guard guard(mutex);
    exit = true;
    cv.notify_all();
  }

  template <typename Fn>
  void produce(bool notify_multiple, Fn&& callback) {
    std::unique_lock guard(mutex);

    callback(container);

    if (notify_multiple) {
      cv.notify_all();
    } else {
      cv.notify_one();
    }
  }

  template <typename Fn>
  bool consume_blocking(Fn&& callback) {
    std::unique_lock guard(mutex);

    cv.wait(guard, [&]() { return exit || !container.empty(); });

    if (exit) {
      return false;
    }

    callback(container);

    return true;
  }

  template <typename Fn>
  void consume_non_blocking(Fn&& callback) {
    std::unique_lock guard(mutex);
    callback(container);
  }
};

}  // namespace base