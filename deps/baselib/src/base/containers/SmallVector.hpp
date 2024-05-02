#pragma once
#include <bit>
#include <memory>
#include <span>
#include <type_traits>

#include <base/Error.hpp>

namespace base {

template <typename T>
class SmallVectorImpl {
 protected:
  using StorageT = std::aligned_storage_t<sizeof(T), alignof(T)>;

  StorageT* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;

  SmallVectorImpl(StorageT* data, size_t capacity) : data_(data), capacity_(capacity) {}

  StorageT* get_inline_storage() {
    const auto end = uintptr_t(this) + sizeof(SmallVectorImpl<T>);
    const auto align = alignof(T);
    const auto unalignment = end & (align - 1);
    const auto storage = unalignment == 0 ? end : (end + (alignof(T) - unalignment));

    return reinterpret_cast<StorageT*>(storage);
  }

  bool is_using_inline_storage() { return data_ == get_inline_storage(); }

  void ensure_capacity(size_t required_capacity) {
    if (capacity_ < required_capacity) {
      const auto is_pow2 = (required_capacity & (required_capacity - 1)) == 0;
      const auto new_capacity =
        is_pow2 ? required_capacity
                : (size_t(1) << (sizeof(size_t) * 8 - std::countl_zero(required_capacity)));
      const auto new_storage = new StorageT[new_capacity];

      std::uninitialized_copy(std::make_move_iterator(begin()), std::make_move_iterator(end()),
                              reinterpret_cast<T*>(new_storage));

      if (!is_using_inline_storage()) {
        delete[] data_;
      }

      capacity_ = new_capacity;
      data_ = new_storage;
    }
  }

  StorageT* free_element() {
    ensure_capacity(size() + 1);
    return &data_[size_];
  }

  void copy_from(const SmallVectorImpl& other) {
    ensure_capacity(other.size());
    std::uninitialized_copy(other.begin(), other.end(), begin());
    size_ = other.size();
  }

 public:
  SmallVectorImpl& operator=(const SmallVectorImpl& other) {
    if (this != &other) {
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

  void reserve(size_t capacity) { ensure_capacity(capacity); }

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
      ensure_capacity(new_size);

      const auto to_add = new_size - size_;
      const auto old_size = size_;
      for (size_t i = 0; i < to_add; ++i) {
        new (data() + old_size + i) T(default_value);
      }
      size_ = new_size;
    }
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return size_ == 0; }

  T* data() { return reinterpret_cast<T*>(data_); }
  const T* data() const { return reinterpret_cast<const T*>(data_); }

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

  bool operator==(const SmallVectorImpl<T>& other) const {
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

  bool operator!=(const SmallVectorImpl<T>& other) const { return !(this == other); }
};

template <typename T, size_t N>
class SmallVector : public SmallVectorImpl<T> {
  using StorageT = typename SmallVectorImpl<T>::StorageT;

  StorageT inline_storage[N];

  template <size_t OtherN>
  void move_from(SmallVector<T, OtherN>&& other) {
    if (other.is_using_inline_storage()) {
      this->ensure_capacity(other.size());

      std::uninitialized_copy(std::make_move_iterator(other.begin()),
                              std::make_move_iterator(other.end()), this->begin());

      this->size_ = other.size();
      other.size_ = 0;
    } else {
      if (this->data_ != inline_storage) {
        delete[] this->data_;
      }

      this->data_ = other.data_;
      this->capacity_ = other.capacity_;
      this->size_ = other.size_;

      other.data_ = other.inline_storage;
      other.capacity_ = OtherN;
      other.size_ = 0;
    }
  }

 public:
  using value_type = T;

  SmallVector() : SmallVectorImpl<T>(inline_storage, N) {
    verify(this->is_using_inline_storage(), "offset calculation is incorrect");
  }

  SmallVector(const SmallVectorImpl<T>& other) : SmallVector() { this->copy_from(other); }

  // implemented by SmallVectorImpl:
  // SmallVector& operator=(const SmallVectorImpl<T>& other)

  template <size_t OtherN>
  SmallVector(SmallVector<T, OtherN>&& other) : SmallVector() {
    move_from(std::move(other));
  }

  template <size_t OtherN>
  SmallVector& operator=(SmallVector<T, OtherN>&& other) {
    if (this != &other) {
      this->clear();
      move_from(std::move(other));
    }
    return *this;
  }

  ~SmallVector() {
    this->clear();

    if (this->data_ != inline_storage) {
      delete[] this->data_;
    }
  }
};

}  // namespace base