#pragma once
#include <base/ClassTraits.hpp>

#include <vector>

#include "Cache.hpp"

namespace base {

template <typename T>
class PerThreadStorage {
  std::vector<CacheLineAligned<T>> storage{1};

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(PerThreadStorage)

  PerThreadStorage() = default;

  void update_thread_count(size_t count) {
    if (count > storage.size()) {
      storage.resize(count);
    }
  }

  T& get(size_t tid) { return storage[tid].get(); }
};

}  // namespace base