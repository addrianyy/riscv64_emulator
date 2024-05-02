#pragma once
#include <fmt/core.h>
#include <base/Format.hpp>

namespace base {

template <typename... Args>
inline void print(format_string<Args...> fmt, Args&&... args) {
  return ::fmt::print(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void println(format_string<Args...> fmt, Args&&... args) {
  ::fmt::println(fmt, std::forward<Args>(args)...);
}

inline void println() {
  ::fmt::print("\n");
}

}  // namespace base