#pragma once
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace base {

template <typename T>
bool parse_integer(std::string_view s, T& value, int base = 10) {
  value = {};

  const auto begin = s.data();
  const auto end = s.data() + s.size();

  const auto result = std::from_chars(begin, end, value, base);
  return result.ec == std::errc() && result.ptr == end;
}

template <typename T>
bool parse_integer(const char* s, T& value, int base = 10) {
  return parse_integer(std::string_view{s, s + std::strlen(s)}, value, base);
}

}  // namespace base