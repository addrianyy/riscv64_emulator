#pragma once
#include <vector>

#include "Registers.hpp"

namespace vm::jit::x64 {

struct Abi {
  std::vector<X64R> callee_saved_regs{};
  X64R argument_reg{};

  static Abi windows();
  static Abi linux();
  static Abi macos();
};

}  // namespace vm::jit::x64