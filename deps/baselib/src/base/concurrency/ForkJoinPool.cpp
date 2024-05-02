#include "ForkJoinPool.hpp"

using base::detail::BaseForkJoinPool;

void BaseForkJoinPool::worker_thread(uint32_t tid) {
  while (true) {
    Task* task = nullptr;

    {
      std::unique_lock lock(task_mutex);
      task_cv.wait(lock, [this, tid] { return requested_exit || thread_tasks[tid].get(); });
      if (requested_exit) {
        return;
      }

      task = thread_tasks[tid].get();

      thread_tasks[tid] = {};
    }

    task->execute(tid);

    {
      std::unique_lock lock(finished_mutex);
      if (++finished_threads == threads.size()) {
        finished_cv.notify_one();
      }
    }
  }
}

BaseForkJoinPool::BaseForkJoinPool(size_t thread_count) {
  thread_count = std::max(thread_count, size_t(1));

  thread_tasks.resize(thread_count);

  for (size_t tid = 0; tid < thread_count; ++tid) {
    threads.spawn([this, tid] { worker_thread(uint32_t(tid)); });
  }
}

BaseForkJoinPool::BaseForkJoinPool() : BaseForkJoinPool(std::thread::hardware_concurrency()) {}

BaseForkJoinPool::~BaseForkJoinPool() {
  exit();
}

void BaseForkJoinPool::run_task(Task& task) {
  task.prepare(threads.size());

  {
    std::unique_lock lock(task_mutex);
    for (auto& thread_task : thread_tasks) {
      thread_task = &task;
    }
    task_cv.notify_all();
  }

  {
    std::unique_lock lock(finished_mutex);
    finished_cv.wait(lock, [this] { return finished_threads == threads.size(); });
  }

  finished_threads = 0;
}

void BaseForkJoinPool::exit() {
  if (!threads.empty()) {
    std::unique_lock lock(task_mutex);
    requested_exit = true;
    task_cv.notify_all();
  }

  threads.join();
}
