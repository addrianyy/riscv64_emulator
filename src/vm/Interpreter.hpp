#pragma once
#include "Cpu.hpp"
#include "Exit.hpp"
#include "Memory.hpp"

namespace vm {

class Interpreter {
 public:
  static bool step(Memory& memory, Cpu& cpu, Exit& exit);
};

}  // namespace vm