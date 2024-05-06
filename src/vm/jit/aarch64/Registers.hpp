#pragma once
#include <asmlib_a64/Types.hpp>

#include <iterator>

namespace vm::jit::aarch64 {

using A64R = a64::Register;

class RegisterAllocation {
 public:
  constexpr static auto exit_reason = A64R::X0;
  constexpr static auto exit_pc = A64R::X1;

  constexpr static auto register_state = A64R::X0;
  constexpr static auto memory_base = A64R::X1;
  constexpr static auto permissions_base = A64R::X2;
  constexpr static auto memory_size = A64R::X3;
  constexpr static auto block_base = A64R::X4;
  constexpr static auto max_executable_pc = A64R::X5;
  constexpr static auto code_base = A64R::X6;
  constexpr static auto base_pc = A64R::X7;

  constexpr static auto a_reg = A64R::X8;
  constexpr static auto b_reg = A64R::X9;
  constexpr static auto c_reg = A64R::X10;

  constexpr static auto trampoline_block = a_reg;

  constexpr static A64R cache[]{
    A64R::X11, A64R::X12, A64R::X13, A64R::X14, A64R::X15, A64R::X16,
    A64R::X17, A64R::X19, A64R::X20, A64R::X21, A64R::X22, A64R::X23,
    A64R::X24, A64R::X25, A64R::X26, A64R::X27, A64R::X28,
  };
  constexpr static size_t cache_size = std::size(cache);
};

}  // namespace vm::jit::aarch64