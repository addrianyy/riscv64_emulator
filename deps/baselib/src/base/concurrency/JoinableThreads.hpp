#pragma once
#include <base/ClassTraits.hpp>

#include <thread>
#include <vector>

namespace base {

class JoinableThreads {
  std::vector<std::thread> threads;

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(JoinableThreads)

  JoinableThreads() = default;
  ~JoinableThreads() { join(); }

  bool empty() const { return threads.empty(); }
  size_t size() const { return threads.size(); }

  template <typename Fn>
  void spawn(Fn&& fn) {
    threads.emplace_back(std::forward<Fn>(fn));
  }

  void join() {
    for (auto& thread : threads) {
      thread.join();
    }

    threads.clear();
  }
};

}  // namespace base