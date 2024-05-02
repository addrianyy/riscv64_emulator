#pragma once
#include "JitCodeBuffer.hpp"

#include <vm/Cpu.hpp>
#include <vm/Memory.hpp>

#include <memory>

namespace vm {

enum class OutlineHandlerID {
  MemoryTranslate8,
  MemoryTranslate16,
  MemoryTranslate32,
  MemoryTranslate64,
  StaticBranch,
  DynamicBranch,
  Max,
};

struct OutlineHandlers {
  uint64_t addresses[size_t(OutlineHandlerID::Max)]{};
};

class JitCodeDump;

class JitExecutor {
  std::shared_ptr<JitCodeBuffer> code_buffer;
  std::unique_ptr<JitCodeDump> code_dump;

  OutlineHandlers outline_handlers{};
  void* trampoline_fn = nullptr;

  void* generate_code(const Memory& memory, uint64_t pc);

  void generate_trampoline();

 public:
  explicit JitExecutor(std::shared_ptr<JitCodeBuffer> code_buffer);
  ~JitExecutor();

  void run(Memory& memory, Cpu& cpu);
};

}  // namespace vm