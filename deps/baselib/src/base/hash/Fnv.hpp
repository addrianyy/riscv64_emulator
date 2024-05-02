#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace base {

class Fnv1a {
  uint64_t hash_ = 0xcbf29ce484222325;

 public:
  Fnv1a() = default;

  template <typename T>
  void feed(const T& data) {
    static_assert(std::is_trivial_v<T>, "feed works only with trivial types");
    feed(&data, sizeof(T));
  }

  void feed(const void* data, size_t size) {
    const auto bytes = reinterpret_cast<const uint8_t*>(data);

    for (size_t i = 0; i < size; ++i) {
      hash_ = (hash_ ^ uint64_t(bytes[i])) * uint64_t(0x100000001b3);
    }
  }

  uint64_t hash() const { return hash_; }
};

}  // namespace base