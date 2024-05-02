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

  while (true) {
    jit_executor->run(memory_, cpu);
    if (!Interpreter::step(memory_, cpu, exit)) {
      return exit;
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
