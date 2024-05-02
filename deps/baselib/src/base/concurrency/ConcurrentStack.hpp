#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace base {

template <typename T>
class ConcurrentStack {
  bool requested_exit = false;

  std::vector<T> stack;
  std::mutex mutex;
  std::condition_variable cv;

 public:
  ConcurrentStack() = default;

  void request_exit() {
    std::lock_guard lock(mutex);
    requested_exit = true;
    cv.notify_all();
  }

  void push_back_one(T&& data) {
    std::unique_lock lock(mutex);
    stack.push_back(std::move(data));
    cv.notify_one();
  }

  void push_back_many(std::vector<T>& data) {
    if (data.empty()) {
      return;
    }

    std::unique_lock lock(mutex);

    stack.reserve(stack.size() + data.size());
    for (auto& v : data) {
      stack.push_back(std::move(v));
    }

    cv.notify_all();
  }

  bool pop_back_blocking(T& output) {
    std::unique_lock lock(mutex);
    cv.wait(lock, [this] { return !stack.empty() || requested_exit; });
    if (requested_exit) {
      return false;
    }

    output = std::move(stack.back());
    stack.pop_back();

    return true;
  }

  void clear() {
    std::unique_lock lock(mutex);
    stack.clear();
  }
};

}  // namespace base