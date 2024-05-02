#pragma once
#include <cstddef>
#include <utility>

namespace base {

constexpr static size_t max_cache_line_size = 32;

template <typename T>
class alignas(max_cache_line_size) CacheLineAligned {
  T data;

 public:
  CacheLineAligned() = default;
  CacheLineAligned(T&& data) : data(std::move(data)) {}

  T& get() { return data; }
  T& operator*() { return get(); }
  T* operator->() { return &get(); }

  const T& get() const { return data; }
  const T& operator*() const { return get(); }
  const T* operator->() const { return &get(); }
};

}  // namespace base