#include "Exit.hpp"

#include <base/Error.hpp>

using namespace vm;

std::string_view detail::exit_reason_representation(Exit::Reason reason) {
  using R = Exit::Reason;

  switch (reason) {
      // clang-format off
    case R::None: return "None";
    case R::UnalignedPc: return "UnalignedPc";
    case R::OutOfBoundsPc: return "OutOfBoundsPc";
    case R::InstructionFetchFault: return "InstructionFetchFault";
    case R::UndefinedInstruction: return "UndefinedInstruction";
    case R::MemoryReadFault: return "MemoryReadFault";
    case R::MemoryWriteFault: return "MemoryWriteFault";
    case R::Ecall: return "Ecall";
    case R::Ebreak: return "Ebreak";
      // clang-format on

    default:
      unreachable();
  }
}