#pragma once

namespace base {

template <class... Args>
struct Overload : Args... {
  using Args::operator()...;
};

template <class... Args>
Overload(Args...) -> Overload<Args...>;

}  // namespace base