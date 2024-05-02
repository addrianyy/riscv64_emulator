#pragma once
#include "Exit.hpp"
#include "Memory.hpp"
#include "jit/JitCodeBuffer.hpp"

#include <memory>

namespace vm {

class Cpu;
class JitExecutor;

class Vm {
  Memory memory_;
  std::unique_ptr<JitExecutor> jit_executor;

 public:
  explicit Vm(size_t memory_size);
  ~Vm();

  void use_jit(std::shared_ptr<JitCodeBuffer> code_buffer);

  Exit run(Cpu& cpu);
  Exit run_interpreter(Cpu& cpu);

  Memory& memory() { return memory_; }
  const Memory& memory() const { return memory_; }
};

}  // namespace vm