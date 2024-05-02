#pragma once
#include <base/ClassTraits.hpp>

#include <type_traits>
#include <utility>

namespace base {

template <typename Fn>
class Deferred {
  Fn fn;

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(Deferred)

  explicit Deferred(Fn&& fn) : fn(std::forward<Fn>(fn)) {}
  ~Deferred() { fn(); }
};

}  // namespace base