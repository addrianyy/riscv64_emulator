#pragma once
#include <base/Format.hpp>
#include <string>

namespace base::detail::error {

[[noreturn]] void do_fatal_error(const char* file, int line, const std::string& message);
[[noreturn]] void do_verify_fail(const char* file, int line, const std::string& message);

template <typename... Args>
[[noreturn]] inline void fatal_error_fmt(const char* file,
                                         int line,
                                         base::format_string<Args...> fmt,
                                         Args&&... args) {
  do_fatal_error(file, line, base::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
inline void verify_fmt(const char* file,
                       int line,
                       bool value,
                       base::format_string<Args...> fmt,
                       Args&&... args) {
  if (!value) {
    do_verify_fail(file, line, base::format(fmt, std::forward<Args>(args)...));
  }
}

}  // namespace base::detail::error

#define fatal_error(format, ...) \
  ::base::detail::error::fatal_error_fmt(__FILE__, __LINE__, (format), ##__VA_ARGS__)

#define verify(value, format, ...) \
  ::base::detail::error::verify_fmt(__FILE__, __LINE__, !!(value), (format), ##__VA_ARGS__)

#define unreachable() fatal_error("entered unreachable code")
