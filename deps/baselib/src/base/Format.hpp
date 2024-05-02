#pragma once
#include <fmt/format.h>
#include <utility>

namespace base {

using memory_buffer = fmt::memory_buffer;

template <typename... Args>
using format_string = ::fmt::format_string<Args...>;

template <typename... Args>
[[nodiscard]] inline std::string format(format_string<Args...> fmt, Args&&... args) {
  return ::fmt::format(fmt, std::forward<Args>(args)...);
}

template <typename OutputIt, typename... Args>
inline OutputIt format_to(OutputIt out, format_string<Args...> fmt, Args&&... args) {
  return ::fmt::format_to(out, fmt, std::forward<Args>(args)...);
}

}  // namespace base