#pragma once
#include <memory>
#include <span>
#include <type_traits>

#include <base/Error.hpp>

namespace base {

template <typename T, size_t N>
class StaticVector {
  static_assert(N > 0, "empty StaticVector is not supported");

  using StorageT = std::aligned_storage_t<sizeof(T), alignof(T)>;

  StorageT storage[N];
  size_t size_ = 0;

  StorageT* free_element() {
    if (size_ >= N) {
      fatal_error("StaticVector is already full");
    }

    return &storage[size_];
  }

  void move_from(StaticVector&& other) noexcept {
    std::uninitialized_copy(std::make_move_iterator(other.begin()),
                            std::make_move_iterator(other.end()), begin());
    size_ = other.size_;
    other.size_ = 0;
  }

  void copy_from(const StaticVector& other) noexcept {
    std::uninitialized_copy(other.begin(), other.end(), begin());
    size_ = other.size_;
  }

 public:
  using value_type = T;

  StaticVector() = default;

  template <size_t InitN>
  StaticVector(const T (&array)[InitN]) {
    static_assert(InitN <= N);

    for (auto v : array) {
      push_back(std::move(v));
    }
  }

  StaticVector(std::initializer_list<T> input) {
    for (auto v : input) {
      push_back(std::move(v));
    }
  }

  ~StaticVector() { clear(); }

  StaticVector(StaticVector&& other) noexcept { move_from(std::move(other)); }
  StaticVector(const StaticVector& other) { copy_from(other); }

  StaticVector& operator=(StaticVector&& other) noexcept {
    if (&other != this) {
      clear();
      move_from(std::move(other));
    }
    return *this;
  }

  StaticVector& operator=(const StaticVector& other) {
    if (&other != this) {
      clear();
      copy_from(other);
    }
    return *this;
  }

  void clear() {
    while (size_ > 0) {
      (*this)[--size_].~T();
    }
  }

  void resize(size_t new_size) {
    if (size() != new_size) {
      resize(new_size, T{});
    }
  }

  void resize(size_t new_size, const T& default_value) {
    while (size() > new_size) {
      pop_back();
    }

    if (new_size > size_) {
      verify(new_size <= N, "out of bounds resize");

      const auto to_add = new_size - size_;
      const auto old_size = size_;
      for (size_t i = 0; i < to_add; ++i) {
        new (data() + old_size + i) T(default_value);
      }
      size_ = new_size;
    }
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T* data() { return reinterpret_cast<T*>(storage); }
  const T* data() const { return reinterpret_cast<const T*>(storage); }

  operator std::span<T>() { return std::span<T>(data(), size_); }
  operator std::span<const T>() const { return std::span<const T>(data(), size_); }

  T* begin() { return data(); }
  T* end() { return data() + size_; }
  const T* begin() const { return data(); }
  const T* end() const { return data() + size_; }

  T& front() { return data()[0]; }
  T& back() { return data()[size_ - 1]; }
  const T& front() const { return data()[0]; }
  const T& back() const { return data()[size_ - 1]; }

  T& operator[](size_t index) { return data()[index]; }
  const T& operator[](size_t index) const { return data()[index]; }

  void push_back(T&& value) {
    new (free_element()) T(std::move(value));
    size_++;
  }

  void push_back(const T& value) {
    new (free_element()) T(value);
    size_++;
  }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    new (free_element()) T(std::forward<Args>(args)...);
    size_++;

    return back();
  }

  void pop_back() {
    size_--;
    end()->~T();
  }

  template <size_t OtherN>
  bool operator==(const StaticVector<T, OtherN>& other) const {
    if (other.size() != size()) {
      return false;
    }

    for (size_t i = 0; i < size(); ++i) {
      if ((*this)[i] != other[i]) {
        return false;
      }
    }

    return true;
  }

  template <size_t OtherN>
  bool operator!=(const StaticVector<T, OtherN>& other) const {
    return !(this == other);
  }
};

}  // namespace base