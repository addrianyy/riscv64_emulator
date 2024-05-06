#include "CreateExecutor.hpp"

using namespace vm;
using namespace vm::jit;

#ifdef VM_JIT_AARCH64

#include "aarch64/Executor.hpp"

std::unique_ptr<Executor> jit::create_arch_specific_executor(
  std::shared_ptr<CodeBuffer> code_buffer,
  std::unique_ptr<CodeDump> code_dump) {
  return std::make_unique<aarch64::Executor>(std::move(code_buffer), std::move(code_dump));
}

#else

std::unique_ptr<Executor> jit::create_arch_specific_executor(
  std::shared_ptr<CodeBuffer> code_buffer,
  std::unique_ptr<CodeDump> code_dump) {
  return nullptr;
}

#endif
