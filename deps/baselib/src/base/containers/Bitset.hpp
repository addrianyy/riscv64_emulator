#pragma once
#include <bit>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace base {

template <typename TStorage>
class Bitset {
  static_assert(std::is_same_v<typename TStorage::value_type, uint64_t>,
                "vector type must be uint64_t");

  TStorage set;

 public:
  Bitset() = default;
  template <typename... Args>
  explicit Bitset(size_t n, Args&&... args) : set(std::forward<Args>(args)...) {
    resize(n);
  }

  void insert(size_t i) { set[i / 64] |= (uint64_t(1) << (i % 64)); }
  void remove(size_t i) { set[i / 64] &= ~(uint64_t(1) << (i % 64)); }
  bool contains(size_t i) const { return set[i / 64] & (uint64_t(1) << (i % 64)); }

  std::optional<size_t> find_first_index_set() const {
    for (size_t i = 0; i < set.size(); ++i) {
      if (set[i] == 0) {
        continue;
      }

      const auto bit = std::countr_zero(set[i]);
      return i * 64 + bit;
    }

    return std::nullopt;
  }

  void resize(size_t n) {
    const auto new_size = (n + 63) / 64;
    if (new_size > set.size()) {
      set.resize(new_size);
    }
  }

  void clear() {
    for (size_t i = 0; i < set.size(); ++i) {
      set[i] = 0;
    }
  }
};

}  // namespace base