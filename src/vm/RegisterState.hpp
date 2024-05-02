#pragma once
#include <cstddef>
#include <cstdint>

#include "Register.hpp"

namespace vm {

class RegisterState {
  uint64_t registers[33]{};

 public:
  RegisterState() = default;

  uint64_t get(Register reg) const { return registers[size_t(reg)]; }

  void set(Register reg, uint64_t value) {
    if (reg != Register::Zero) {
      registers[size_t(reg)] = value;
    }
  }

  uint64_t pc() const { return get(Register::Pc); }

  uint64_t* raw_table() { return registers; }
};

}  // namespace vm