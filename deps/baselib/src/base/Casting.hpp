#pragma once
#include <type_traits>

namespace base {

namespace detail {
template <typename To, typename From>
using CastResult = std::conditional_t<std::is_const_v<From>, const To*, To*>;
}

template <typename To, typename From>
detail::CastResult<To, From> relaxed_cast(From* from) {
  if constexpr (std::is_base_of_v<To, From>) {
    // We are upcasting, it will always succeed.
    return from;
  } else if constexpr (!std::is_base_of_v<From, To>) {
    // Pointer types are unrelated, the cast will always fail.
    return nullptr;
  } else {
    if (!from) {
      return nullptr;
    }
    return To::isa(from) ? static_cast<detail::CastResult<To, From>>(from) : nullptr;
  }
}

template <typename To, typename From>
detail::CastResult<To, From> cast(From* from) {
  static_assert(std::is_base_of_v<To, From> || std::is_base_of_v<From, To>,
                "casting between unrelated pointer types  will always fail");

  return relaxed_cast<To, From>(from);
}

template <typename To, typename From>
detail::CastResult<To, From> cast(From& from) {
  return cast<To, From>(&from);
}

}  // namespace base