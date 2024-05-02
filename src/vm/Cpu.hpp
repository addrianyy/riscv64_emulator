#pragma once
#include "RegisterState.hpp"

namespace vm {

class Cpu {
  RegisterState registers;

 public:
  uint64_t reg(Register reg) const { return registers.get(reg); }
  void set_reg(Register reg, uint64_t value) { return registers.set(reg, value); }

  uint64_t pc() const { return reg(Register::Pc); }

  RegisterState& register_state() { return registers; }
  const RegisterState& register_state() const { return registers; }
};

}  // namespace vm