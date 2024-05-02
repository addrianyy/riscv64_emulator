#pragma once
#include <fmt/format.h>

namespace vm {

enum class Register {
  Zero = 0,
  Ra,
  Sp,
  Gp,
  Tp,
  T0,
  T1,
  T2,
  S0,
  S1,
  A0,
  A1,
  A2,
  A3,
  A4,
  A5,
  A6,
  A7,
  S2,
  S3,
  S4,
  S5,
  S6,
  S7,
  S8,
  S9,
  S10,
  S11,
  T3,
  T4,
  T5,
  T6 = 31,
  Pc = 32,
};

namespace detail {

std::string_view register_name(Register reg);

}

}  // namespace vm

template <>
struct fmt::formatter<vm::Register> : formatter<std::string_view> {
  auto format(vm::Register reg, format_context& ctx) const {
    return formatter<string_view>::format(vm::detail::register_name(reg), ctx);
  }
};