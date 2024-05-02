#pragma once
#include <array>
#include <cstdint>

#include <base/Format.hpp>

namespace base {

class PreciseTime {
  uint64_t nano{};

  constexpr explicit PreciseTime(uint64_t nanoseconds) : nano(nanoseconds) {}

 public:
  constexpr PreciseTime() = default;

  static PreciseTime now();

  static constexpr PreciseTime from_nanoseconds(uint64_t ns) { return PreciseTime{ns}; }
  static constexpr PreciseTime from_microseconds(uint64_t us) { return PreciseTime{us * 1'000}; }
  static constexpr PreciseTime from_milliseconds(uint64_t ms) {
    return PreciseTime{ms * 1'000'000};
  }
  static constexpr PreciseTime from_seconds(double s) {
    return PreciseTime{uint64_t(s * 1'000'000'000.0)};
  }

  constexpr uint64_t nanoseconds() const { return nano; }
  constexpr uint64_t microseconds() const { return nano / 1'000; }
  constexpr uint64_t milliseconds() const { return nano / 1'000'000; }
  constexpr double seconds() const { return double(nano) / 1'000'000'000.0; }

  constexpr PreciseTime operator+(const PreciseTime& other) const {
    return PreciseTime(nano + other.nano);
  }
  constexpr PreciseTime operator-(const PreciseTime& other) const {
    return PreciseTime(nano - other.nano);
  }
  constexpr PreciseTime operator*(uint64_t scale) const { return PreciseTime(nano * scale); }
  constexpr PreciseTime operator/(uint64_t scale) const { return PreciseTime(nano / scale); }

  constexpr PreciseTime& operator+=(const PreciseTime& other) {
    nano += other.nano;
    return *this;
  }

  constexpr PreciseTime& operator-=(const PreciseTime& other) {
    nano -= other.nano;
    return *this;
  }

  constexpr PreciseTime& operator*=(uint64_t scale) {
    nano *= scale;
    return *this;
  }

  constexpr PreciseTime& operator/=(uint64_t scale) {
    nano /= scale;
    return *this;
  }

  constexpr auto operator<=>(const PreciseTime& other) const = default;
};

}  // namespace base

template <>
struct fmt::formatter<base::PreciseTime> {
  constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator {
    return ctx.begin();
  };

  auto format(const base::PreciseTime& v, format_context& ctx) const -> format_context::iterator {
    auto time_value = v.nanoseconds();
    if (time_value < 1000) {
      return format_to(ctx.out(), "{}ns", time_value);
    } else {
      static constexpr std::array<std::string_view, 3> suffixes{"us", "ms", "s"};

      size_t i = 0;
      while (time_value >= 1'000'000 && i < (suffixes.size() - 1)) {
        time_value /= 1000;
        i++;
      }

      return format_to(ctx.out(), "{}.{:03}{}", time_value / 1000, time_value % 1000, suffixes[i]);
    }
  }
};
