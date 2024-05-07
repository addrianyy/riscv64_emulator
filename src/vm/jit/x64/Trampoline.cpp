#include "Trampoline.hpp"
#include "Registers.hpp"

#include <ranges>

using namespace vm::jit;
using namespace vm::jit::x64;

using asmlib::x64::Memory;

void* x64::generate_trampoline(CodegenContext& context, CodeBuffer& code_buffer, const Abi& abi) {
  using RA = RegisterAllocation;

  auto& as = context.prepare().assembler;

  for (const auto r : abi.callee_saved_regs) {
    as.push(r);
  }

  // Keep the stack 16 byte aligned.
  const auto needs_extra_push = ((abi.callee_saved_regs.size() + 1) % 16) == 0;

  as.mov(RA::trampoline_block, abi.argument_reg);

  as.mov(RA::register_state,
         Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, register_state)));
  as.mov(RA::memory_base,
         Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, memory_base)));
  as.mov(RA::permissions_base,
         Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, permissions_base)));
  as.mov(RA::code_base,
         Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, code_base)));
  as.mov(RA::block_base,
         Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, block_base)));

  as.push(RA::trampoline_block);
  if (needs_extra_push) {
    as.push(RA::trampoline_block);
  }

  as.call(Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, entrypoint)));

  as.pop(RA::trampoline_block);
  if (needs_extra_push) {
    as.pop(RA::trampoline_block);
  }

  as.mov(Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, exit_reason)),
         RA::exit_reason);
  as.mov(Memory::base_disp(RA::trampoline_block, offsetof(TrampolineBlock, exit_pc)), RA::exit_pc);

  for (const auto r : std::ranges::reverse_view(abi.callee_saved_regs)) {
    as.pop(r);
  }

  as.ret();

  return code_buffer.insert_standalone(as.assembled_instructions());
}
