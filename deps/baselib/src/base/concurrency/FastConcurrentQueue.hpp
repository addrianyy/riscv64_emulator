#pragma once
#include <condition_variable>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace base {

template <typename T>
class FastConcurrentQueue {
  bool requested_exit = false;

  std::vector<T> queue;
  std::mutex mutex;
  std::condition_variable cv;

 public:
  FastConcurrentQueue() = default;

  void clear() {
    std::unique_lock lock(mutex);
    queue.clear();
  }

  void request_exit() {
    std::lock_guard lock(mutex);
    requested_exit = true;
    cv.notify_all();
  }

  void push_back_one(T&& data) {
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

      queue.reserve(queue.size() + data.size());
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

      queue.reserve(queue.size() + data_size);
      for (size_t i = 0; i < data_size; ++i) {
        queue.push_back(data_source(i));
      }

      cv.notify_all();
    }
  }

  void pop_front_non_blocking(std::vector<T>& output) {
    std::unique_lock lock(mutex);

    output.reserve(output.size() + queue.size());
    for (auto& v : queue) {
      output.push_back(std::move(v));
    }

    queue.clear();
  }

  bool pop_front_blocking(std::vector<T>& output) {
    std::unique_lock lock(mutex);

    cv.wait(lock, [this] { return !queue.empty() || requested_exit; });
    if (requested_exit) {
      return false;
    }

    output.reserve(output.size() + queue.size());
    for (auto& v : queue) {
      output.push_back(std::move(v));
    }

    queue.clear();

    return true;
  }
};

}  // namespace base