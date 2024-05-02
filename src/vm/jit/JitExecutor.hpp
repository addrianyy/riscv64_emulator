#pragma once
#include "JitCodeBuffer.hpp"

#include <vm/Cpu.hpp>
#include <vm/Memory.hpp>

#include <memory>

namespace vm {

enum class JitExitReason {
  UnalignedPc,
  OutOfBoundsPc,
  InstructionFetchFault,
  UndefinedInstruction,
  UnsupportedInstruction,
  MemoryReadFault,
  MemoryWriteFault,
  Ecall,
  Ebreak,
};

class JitCodeDump;

class JitExecutor {
  std::shared_ptr<JitCodeBuffer> code_buffer;
  std::unique_ptr<JitCodeDump> code_dump;

  void* trampoline_fn = nullptr;

  void* generate_code(const Memory& memory, uint64_t pc);

  void generate_trampoline();

 public:
  explicit JitExecutor(std::shared_ptr<JitCodeBuffer> code_buffer);
  ~JitExecutor();

  JitExitReason run(Memory& memory, Cpu& cpu);
};

}  // namespace vm