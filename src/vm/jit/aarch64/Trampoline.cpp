#include "Trampoline.hpp"
#include "Utilities.hpp"

using namespace vm::jit;
using namespace vm::jit::aarch64;

void* aarch64::generate_trampoline(CodegenContext& context, CodeBuffer& code_buffer) {
  using RA = RegisterAllocation;

  auto& as = context.prepare().assembler;

  constexpr auto block_reg = A64R::X10;

  as.mov(block_reg, A64R::X0);

  as.stp(block_reg, A64R::X30, A64R::Sp, -16, a64::Writeback::Pre);

  as.ldr(RA::register_state, block_reg, offsetof(TrampolineBlock, register_state));
  as.ldr(RA::memory_base, block_reg, offsetof(TrampolineBlock, memory_base));
  as.ldr(RA::memory_size, block_reg, offsetof(TrampolineBlock, memory_size));
  as.ldr(RA::block_base, block_reg, offsetof(TrampolineBlock, block_base));
  as.ldr(RA::max_executable_pc, block_reg, offsetof(TrampolineBlock, max_executable_pc));
  as.ldr(RA::code_base, block_reg, offsetof(TrampolineBlock, code_base));

  as.ldr(block_reg, block_reg, offsetof(TrampolineBlock, entrypoint));
  as.blr(block_reg);

  as.ldp(block_reg, A64R::X30, A64R::Sp, 16, a64::Writeback::Post);

  as.str(RA::exit_reason, block_reg, offsetof(TrampolineBlock, exit_reason));
  as.str(RA::exit_pc, block_reg, offsetof(TrampolineBlock, exit_pc));

  as.ret();

  return code_buffer.insert_standalone(
    utils::cast_instructions_to_bytes(as.assembled_instructions()));
}