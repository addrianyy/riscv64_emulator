#pragma once
#include <cstdint>

#include "Register.hpp"

namespace vm {

struct Exit {
  enum class Reason {
    None,
    UnalignedPc,
    InstructionFetchFault,
    UndefinedInstruction,
    MemoryReadFault,
    MemoryWriteFault,
    Ecall,
    Ebreak,
  };
  Reason reason{};
  uint64_t faulty_address{};
  Register target_register{};
};

}  // namespace vm