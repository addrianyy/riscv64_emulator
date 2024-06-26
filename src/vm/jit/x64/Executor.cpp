#include "Executor.hpp"
#include "CodeGenerator.hpp"
#include "Exit.hpp"
#include "Trampoline.hpp"

#include <base/Error.hpp>
#include <base/Log.hpp>

#include <vm/private/ExecutionLog.hpp>

using namespace vm;
using namespace vm::jit::x64;

void* Executor::generate_code(const vm::Memory& memory, uint64_t pc) {
#ifdef PRINT_EXECUTION_LOG
  const bool single_step = true;
#else
  const bool single_step = false;
#endif

  const auto instructions =
    generate_block_code(codegen_context, *code_buffer, memory, single_step, pc);

#ifdef JIT_LOG_GENERATED_BLOCKS
  log_debug("generated code for {:x}: {} bytes...", pc, instructions.size());
#endif

  return code_buffer->insert(pc, instructions);
}

Executor::Executor(std::shared_ptr<CodeBuffer> code_buffer, const Abi& abi)
    : code_buffer(std::move(code_buffer)) {
  trampoline_fn = generate_trampoline(codegen_context, *this->code_buffer, abi);
}

jit::ExitReason Executor::run(Memory& memory, Cpu& cpu) {
  ArchExitReason exit_reason{};

  while (true) {
    const auto pc = cpu.pc();

    auto code = code_buffer->get(pc);
    if (!code) {
      code = generate_code(memory, pc);
      verify(code, "failed to jit code for pc {:x}", pc);
    }

#ifdef PRINT_EXECUTION_LOG
    const auto previous_register_state = cpu.register_state();
#endif

    TrampolineBlock trampoline_block{
      .register_state = uint64_t(cpu.register_state().raw_table()),
      .memory_base = uint64_t(memory.contents()),
      .permissions_base = uint64_t(memory.permissions()),
      .block_base = uint64_t(code_buffer->block_translation_table()),
      .code_base = uint64_t(code_buffer->code_buffer_base()),
      .entrypoint = uint64_t(code),
    };

    reinterpret_cast<void (*)(TrampolineBlock*)>(trampoline_fn)(&trampoline_block);

    cpu.set_reg(Register::Pc, trampoline_block.exit_pc);

#ifdef PRINT_EXECUTION_LOG
    ExecutionLog::print_execution_step(previous_register_state, cpu.register_state());
#endif

    exit_reason = ArchExitReason(trampoline_block.exit_reason);
    if (exit_reason != ArchExitReason::BlockNotGenerated &&
        exit_reason != ArchExitReason::SingleStep) {
      break;
    }
  }

  using I = ArchExitReason;
  using O = ExitReason;

  switch (exit_reason) {
      // clang-format off
    case I::UnalignedPc: return O::UnalignedPc;
    case I::OutOfBoundsPc: return O::OutOfBoundsPc;
    case I::InstructionFetchFault: return O::InstructionFetchFault;
    case I::UndefinedInstruction: return O::UndefinedInstruction;
    case I::UnsupportedInstruction: return O::UnsupportedInstruction;
    case I::MemoryReadFault: return O::MemoryReadFault;
    case I::MemoryWriteFault: return O::MemoryWriteFault;
    case I::Ecall: return O::Ecall;
    case I::Ebreak: return O::Ebreak;
      // clang-format on

    default:
      unreachable();
  }
}
