#pragma once
#include <asmlib_x64/Operand.hpp>

namespace vm::jit::x64 {

using X64R = asmlib::x64::Register;

class RegisterAllocation {
 public:
  constexpr static auto register_state = X64R::Rsi;
  constexpr static auto memory_base = X64R::Rdi;
  constexpr static auto permissions_base = X64R::R8;
  constexpr static auto code_base = X64R::R9;
  constexpr static auto block_base = X64R::R10;

  constexpr static auto trampoline_block = X64R::R11;

  constexpr static auto a_reg = X64R::Rax;
  constexpr static auto b_reg = X64R::Rbx;
  constexpr static auto c_reg = X64R::Rcx;

  constexpr static auto exit_reason = X64R::Rax;
  constexpr static auto exit_pc = X64R::Rbx;
};

}  // namespace vm::jit::x64