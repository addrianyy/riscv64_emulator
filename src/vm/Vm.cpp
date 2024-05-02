#include "Vm.hpp"
#include "Interpreter.hpp"

#include "jit/JitExecutor.hpp"

#include "private/ExecutionLog.hpp"

#include <base/Error.hpp>

using namespace vm;

Vm::Vm(size_t memory_size) : memory_(memory_size) {}
Vm::~Vm() = default;

void Vm::use_jit(std::shared_ptr<JitCodeBuffer> code_buffer) {
  jit_executor = std::make_unique<JitExecutor>(std::move(code_buffer));
}

Exit Vm::run(Cpu& cpu) {
  if (!jit_executor) {
    return run_interpreter(cpu);
  }

  Exit exit{};

  const auto simple_exit = [&](Exit::Reason reason) {
    exit.reason = reason;
    return exit;
  };

  while (true) {
    const auto jit_exit_reason = jit_executor->run(memory_, cpu);

    using JE = JitExitReason;
    switch (jit_exit_reason) {
        // clang-format off
      case JE::UnalignedPc: return simple_exit(Exit::Reason::UnalignedPc);
      case JE::OutOfBoundsPc: return simple_exit(Exit::Reason::OutOfBoundsPc);
      case JE::InstructionFetchFault: return simple_exit(Exit::Reason::InstructionFetchFault);
      case JE::UndefinedInstruction: return simple_exit(Exit::Reason::UndefinedInstruction);
      case JE::Ecall: return simple_exit(Exit::Reason::Ecall);
      case JE::Ebreak: return simple_exit(Exit::Reason::Ebreak);
        // clang-format on

      case JE::UnsupportedInstruction:
      case JE::MemoryReadFault:
      case JE::MemoryWriteFault: {
        if (!Interpreter::step(memory_, cpu, exit)) {
          return exit;
        }
        break;
      }

      default:
        unreachable();
    }
  }
}

Exit Vm::run_interpreter(Cpu& cpu) {
  Exit exit{};
  while (true) {
#ifdef PRINT_EXECUTION_LOG
    const auto previous_register_state = cpu.register_state();
#endif

    if (!Interpreter::step(memory_, cpu, exit)) {
      break;
    }

#ifdef PRINT_EXECUTION_LOG
    ExecutionLog::print_execution_step(previous_register_state, cpu.register_state());
#endif
  }

  verify(exit.reason != vm::Exit::Reason::None,
         "interpreter didn't fill vmexit structure properly");

  return exit;
}
