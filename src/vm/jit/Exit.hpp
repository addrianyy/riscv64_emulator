#pragma once

namespace vm::jit {

enum class ExitReason {
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

}
