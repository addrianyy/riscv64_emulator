#pragma once
#include <base/ClassTraits.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "AtomicIterator.hpp"
#include "Cache.hpp"
#include "JoinableThreads.hpp"
#include "PerThreadStorage.hpp"

namespace base {

namespace detail {

class BaseForkJoinPool {
 public:
  class Task {
   public:
    virtual ~Task() = default;

    virtual void prepare(uint32_t thread_count) {}
    virtual void execute(uint32_t tid) = 0;
  };

 private:
  JoinableThreads threads;

  bool requested_exit = false;
  std::vector<CacheLineAligned<Task*>> thread_tasks;
  std::mutex task_mutex;
  std::condition_variable task_cv;

  uint32_t finished_threads{0};
  std::mutex finished_mutex;
  std::condition_variable finished_cv;

  void worker_thread(uint32_t tid);

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(BaseForkJoinPool)

  explicit BaseForkJoinPool(size_t thread_count);

  BaseForkJoinPool();
  ~BaseForkJoinPool();

  size_t thread_count() const { return threads.size(); }

  void run_task(Task& task);
  void exit();
};

}  // namespace detail

namespace detail::fork_join {

template <typename Fn>
class BasicForTask : public BaseForkJoinPool::Task {
  AtomicIterator<uint64_t> iterator;
  Fn body;

 public:
  explicit BasicForTask(uint64_t count, Fn&& body)
      : iterator(count), body(std::forward<Fn>(body)) {}

  void execute(uint32_t tid) override { iterator.consume(body); }
};

}  // namespace detail::fork_join

class ForkJoinPool : public detail::BaseForkJoinPool {
 public:
  using BaseForkJoinPool::BaseForkJoinPool;

  template <typename Fn>
  void parallel_for(uint64_t count, Fn&& body) {
    detail::fork_join::BasicForTask<Fn> task{count, std::forward<Fn>(body)};
    run_task(task);
  }
};

}  // namespace base