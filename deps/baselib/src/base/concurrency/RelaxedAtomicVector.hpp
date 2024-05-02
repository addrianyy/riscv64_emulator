#pragma once
#include <atomic>
#include <memory>
#include <span>
#include <utility>

#include <base/ClassTraits.hpp>
#include <base/Error.hpp>

namespace base {

template <typename T>
class RelaxedAtomicVector {
  using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

  std::unique_ptr<Storage[]> buffer;
  size_t capacity_{};
  std::atomic_size_t size_{};

  size_t reserve_next() {
    const auto index = size_.fetch_add(1, std::memory_order::relaxed);
#if 1
    verify(index < capacity_, "allocated more than vector capacity");
#endif
    return index;
  }

  bool reserve_next_optional(size_t& index) {
    index = size_.fetch_add(1, std::memory_order::relaxed);
    return index < capacity_;
  }

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(RelaxedAtomicVector)

  RelaxedAtomicVector() = default;
  ~RelaxedAtomicVector() { clear(); }

  T* data() { return reinterpret_cast<T*>(buffer.get()); }
  const T* data() const { return reinterpret_cast<const T*>(buffer.get()); }

  size_t capacity() const { return capacity_; }
  size_t size() const { return std::min(size_.load(std::memory_order::relaxed), capacity_); }
  bool empty() const { return size() == 0; }

  std::span<T> span() { return {data(), size()}; }
  std::span<const T> span() const { return {data(), size()}; }

  operator std::span<T>() { return span(); }
  operator std::span<const T>() const { return span(); }

  T& operator[](size_t i) { return data()[i]; }
  const T& operator[](size_t i) const { return data()[i]; }

  void set_capacity(size_t new_capacity) {
    verify(empty(), "cannot set capacity of non-empty atomic vector");

    if (capacity_ != new_capacity) {
      buffer = std::make_unique<Storage[]>(new_capacity);
      capacity_ = new_capacity;
    }
  }

  void clear() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      const auto element_count = size();
      for (size_t i = 0; i < element_count; ++i) {
        data()[i].~T();
      }
    }
    size_.store(0, std::memory_order::relaxed);
  }

  void push_back(T&& value) {
    const auto p = data() + reserve_next();
    new (p) T(std::move(value));
  }

  void push_back(const T& value) {
    const auto p = data() + reserve_next();
    new (p) T(value);
  }

  bool push_back_optional(T&& value) {
    size_t index;
    if (reserve_next_optional(index)) {
      const auto p = data() + index;
      new (p) T(std::move(value));
      return true;
    }
    return false;
  }

  bool push_back_optional(const T& value) {
    size_t index;
    if (reserve_next_optional(index)) {
      const auto p = data() + index;
      new (p) T(value);
      return true;
    }
    return false;
  }
};

}  // namespace base