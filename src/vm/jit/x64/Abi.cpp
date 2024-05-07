#include "Abi.hpp"

using namespace vm::jit::x64;

Abi Abi::windows() {
  return Abi{
    .callee_saved_regs =
      {
        X64R::Rbx,
        X64R::Rbp,
        X64R::Rdi,
        X64R::Rsi,
        X64R::R12,
        X64R::R13,
        X64R::R14,
        X64R::R15,
      },
    .argument_reg = X64R::Rcx,
  };
}

Abi Abi::linux() {
  return Abi{
    .callee_saved_regs =
      {
        X64R::Rbx,
        X64R::Rbp,
        X64R::R12,
        X64R::R13,
        X64R::R14,
        X64R::R15,
      },
    .argument_reg = X64R::Rdi,
  };
}

Abi Abi::macos() {
  // macOS uses System-V ABI like Linux.
  return linux();
}
