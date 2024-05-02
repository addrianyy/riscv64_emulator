#pragma once
#include <type_traits>

namespace base {

template <typename T>
class IteratorRange {
  T begin_it;
  T end_it;

 public:
  using iterator = T;

  IteratorRange(T begin_it, T end_it) : begin_it(begin_it), end_it(end_it) {}

  T begin() const { return begin_it; }
  T end() const { return end_it; }
};

template <typename T, typename std::enable_if_t<std::is_trivially_copyable_v<T>, int> = 0>
inline IteratorRange<typename T::reverse_iterator> reversed(T object) {
  return IteratorRange(object.rbegin(), object.rend());
}

template <typename T, typename std::enable_if_t<!std::is_trivially_copyable_v<T>, int> = 0>
inline IteratorRange<typename T::reverse_iterator> reversed(T& object) {
  return IteratorRange(object.rbegin(), object.rend());
}

template <typename T>
inline IteratorRange<typename T::const_reverse_iterator> reversed(const T& object) {
  return IteratorRange(object.rbegin(), object.rend());
}

template <typename R, typename Fn>
bool all_of(R&& range, Fn predicate) {
  for (auto&& element : range) {
    if (!predicate(element)) {
      return false;
    }
  }

  return true;
}

template <typename R, typename Fn>
bool any_of(R&& range, Fn predicate) {
  for (auto&& element : range) {
    if (predicate(element)) {
      return true;
    }
  }

  return false;
}

template <typename R, typename Fn>
bool none_of(R&& range, Fn predicate) {
  for (auto&& element : range) {
    if (predicate(element)) {
      return false;
    }
  }

  return true;
}

}  // namespace base