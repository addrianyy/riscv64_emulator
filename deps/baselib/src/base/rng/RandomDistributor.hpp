#pragma once
#include <random>
#include <type_traits>

namespace base {

template <typename Rng>
class RandomDistributor {
  using RngType = typename Rng::result_type;

  static_assert(sizeof(RngType) >= 4, "RNG result type needs to be >= uint32_t");

  Rng rng_;

  RngType rand() {
    if constexpr (std::is_pointer_v<Rng>) {
      return rng_->gen();
    } else {
      return rng_.gen();
    }
  }

  uint32_t rand32() { return uint32_t(rand()); }
  uint32_t rand64() {
    if constexpr (sizeof(RngType) >= 8) {
      return uint64_t(rand());
    } else {
      return (uint64_t(rand()) << 32) | (uint64_t(rand()));
    }
  }

 public:
  explicit RandomDistributor(Rng rng) : rng_(rng) {}

  Rng& rng() { return rng_; }
  const Rng& rng() const { return rng_; }

  float gen_float() {
    std::uniform_real_distribution<float> dist(0.f, 1.f);
    return dist(rng_);
  }

  float gen_float(float max) { return gen_float() * max; }
  float gen_float(float min, float max) { return gen_float() * (max - min) + min; }

  uint32_t gen_uint(uint32_t min, uint32_t max) {
    std::uniform_int_distribution<uint32_t> dist(min, max);
    return dist(rng_);
  }

  uint32_t gen_uint(uint32_t max) { return gen_uint(0, max); }
};

}  // namespace base