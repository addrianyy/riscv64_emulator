#pragma once
#include <cstdint>
#include <limits>

#include "SeedFromSystemRng.hpp"

namespace base {

class Xorshift {
  uint64_t value{};

 public:
  explicit Xorshift(SeedFromSystemRng seed) { reseed(seed); }
  explicit Xorshift(uint64_t seed) { reseed(seed); }

  void reseed(SeedFromSystemRng seed);
  void reseed(uint64_t seed) {
    value = seed;

    for (int i = 0; i < 2; ++i) {
      gen();
    }
  }

  uint64_t gen() {
    uint64_t x = value;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;

    value = x;

    return x;
  }

  using result_type = uint64_t;

  result_type operator()() { return gen(); }

  constexpr static result_type min() { return std::numeric_limits<result_type>::min(); }
  constexpr static result_type max() { return std::numeric_limits<result_type>::max(); }
};

}  // namespace base