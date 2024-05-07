#pragma once
#include <vm/Cpu.hpp>
#include <vm/Memory.hpp>
#include <vm/jit/CodeBuffer.hpp>
#include <vm/jit/Executor.hpp>

#include <memory>

#include "Abi.hpp"
#include "CodegenContext.hpp"

namespace vm::jit::x64 {

class Executor : public jit::Executor {
  std::shared_ptr<CodeBuffer> code_buffer;

  CodegenContext codegen_context;

  void* trampoline_fn = nullptr;

  void* generate_code(const Memory& memory, uint64_t pc);

 public:
  explicit Executor(std::shared_ptr<CodeBuffer> code_buffer, const Abi& abi);

  ExitReason run(Memory& memory, Cpu& cpu) override;
};

}  // namespace vm::jit::x64