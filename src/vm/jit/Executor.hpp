#pragma once
#include "Exit.hpp"

#include <vm/Cpu.hpp>
#include <vm/Memory.hpp>

namespace vm::jit {

class Executor {
 public:
  virtual ~Executor() = default;

  virtual ExitReason run(Memory& memory, Cpu& cpu) = 0;
};

}  // namespace vm::jit
