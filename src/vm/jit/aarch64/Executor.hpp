#pragma once
#include <vm/Cpu.hpp>
#include <vm/Memory.hpp>
#include <vm/jit/CodeBuffer.hpp>
#include <vm/jit/CodeDump.hpp>
#include <vm/jit/Executor.hpp>

#include <memory>

#include "CodegenContext.hpp"

namespace vm::jit::aarch64 {

class Executor : public jit::Executor {
  std::shared_ptr<CodeBuffer> code_buffer;
  std::unique_ptr<CodeDump> code_dump;

  CodegenContext codegen_context;

  void* trampoline_fn = nullptr;

  void* generate_code(const Memory& memory, uint64_t pc);

 public:
  explicit Executor(std::shared_ptr<CodeBuffer> code_buffer, std::unique_ptr<CodeDump> code_dump);

  ExitReason run(Memory& memory, Cpu& cpu) override;
};

}  // namespace vm::jit::aarch64