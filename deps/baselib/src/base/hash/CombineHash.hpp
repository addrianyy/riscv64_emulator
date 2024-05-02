#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace base {

inline void combine_hash_to(std::size_t& seed) {}

template <typename T, typename... Rest>
inline void combine_hash_to(std::size_t& seed, const T& v, Rest... rest) {
  std::hash<T> hasher;
  if constexpr (sizeof(void*) == 8) {
    seed ^= hasher(v) + 0x9e3779b97f4a7c17 + (seed << 6) + (seed >> 2);
  } else {
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }

  combine_hash_to(seed, rest...);
}

template <typename... Args>
inline std::size_t combine_hash(Args... args) {
  std::size_t seed = 0;
  combine_hash_to(seed, args...);

  return seed;
}

}  // namespace base