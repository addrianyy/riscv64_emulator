#pragma once
#include <cstdint>

#include <fmt/format.h>

#include "Register.hpp"

namespace vm {

struct Exit {
  enum class Reason {
    None,
    UnalignedPc,
    OutOfBoundsPc,
    InstructionFetchFault,
    UndefinedInstruction,
    MemoryReadFault,
    MemoryWriteFault,
    Ecall,
    Ebreak,
  };
  Reason reason{};
  uint64_t faulty_address{};
  Register target_register{};
};

namespace detail {

std::string_view exit_reason_representation(Exit::Reason reason);

}

}  // namespace vm

template <>
struct fmt::formatter<vm::Exit::Reason> : formatter<std::string_view> {
  auto format(const vm::Exit::Reason& reason, format_context& ctx) const {
    return formatter<string_view>::format(vm::detail::exit_reason_representation(reason), ctx);
  }
};