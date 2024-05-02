#pragma once
#include <cstdint>
#include <limits>
#include <random>

namespace base {

class SystemRng32 {
  std::random_device device;

 public:
  SystemRng32() = default;

  uint32_t gen() { return device(); }

  using result_type = uint32_t;

  result_type operator()() { return gen(); }

  constexpr static result_type min() { return std::numeric_limits<result_type>::min(); }
  constexpr static result_type max() { return std::numeric_limits<result_type>::max(); }
};

static_assert(std::random_device::min() == SystemRng32::min());
static_assert(std::random_device::max() == SystemRng32::max());

class SystemRng64 {
  base::SystemRng32 rng32;

 public:
  SystemRng64() = default;

  uint64_t gen() { return uint64_t(rng32()) | (uint64_t(rng32()) << 32); }

  using result_type = uint64_t;

  result_type operator()() { return gen(); }

  constexpr static result_type min() { return std::numeric_limits<result_type>::min(); }
  constexpr static result_type max() { return std::numeric_limits<result_type>::max(); }
};

}  // namespace base