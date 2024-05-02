#pragma once
#include "JitCodeBuffer.hpp"

#include <vm/Cpu.hpp>
#include <vm/Memory.hpp>

#include <memory>

namespace vm {

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

  void run(Memory& memory, Cpu& cpu);
};

}  // namespace vm