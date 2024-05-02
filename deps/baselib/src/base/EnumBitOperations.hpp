#pragma once
#include <type_traits>

#define IMPLEMENT_ENUM_BIT_OPERATIONS(type)         \
  constexpr inline type operator|(type a, type b) { \
    using T = std::underlying_type_t<type>;         \
    return type(T(a) | T(b));                       \
  }                                                 \
  constexpr inline type operator&(type a, type b) { \
    using T = std::underlying_type_t<type>;         \
    return type(T(a) & T(b));                       \
  }                                                 \
  constexpr inline type operator~(type a) {         \
    using T = std::underlying_type_t<type>;         \
    return type(~T(a));                             \
  }
