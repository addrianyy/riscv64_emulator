#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

namespace base {

template <typename T>
class ConcurrentQueue {
  bool requested_exit = false;

  std::deque<T> queue;
  std::mutex mutex;
  std::condition_variable cv;

 public:
  ConcurrentQueue() = default;

  void request_exit() {
    std::lock_guard lock(mutex);
    requested_exit = true;
    cv.notify_all();
  }

  void push_back(T&& data) {
    std::unique_lock lock(mutex);
    queue.push_back(std::move(data));
    cv.notify_one();
  }

  void push_back_many(std::span<T> data) {
    if (data.empty()) {
      return;
    }

    {
      std::unique_lock lock(mutex);

      for (auto& v : data) {
        queue.push_back(std::move(v));
      }

      cv.notify_all();
    }
  }

  template <typename Fn>
  void push_back_many_callback(size_t data_size, Fn&& data_source) {
    if (data_size == 0) {
      return;
    }

    {
      std::unique_lock lock(mutex);

      for (size_t i = 0; i < data_size; ++i) {
        queue.push_back(data_source(i));
      }

      cv.notify_all();
    }
  }

  bool pop_front_non_blocking(T& output) {
    std::unique_lock lock(mutex);

    if (!queue.empty()) {
      output = std::move(queue.front());
      queue.pop_front();

      return true;
    }

    return false;
  }

  bool pop_front_blocking(T& output) {
    std::unique_lock lock(mutex);
    cv.wait(lock, [this] { return !queue.empty() || requested_exit; });
    if (requested_exit) {
      return false;
    }

    output = std::move(queue.front());
    queue.pop_front();

    return true;
  }

  void clear() {
    std::unique_lock lock(mutex);
    queue.clear();
  }
};

}  // namespace base