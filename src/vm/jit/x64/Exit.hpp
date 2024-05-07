#pragma once

namespace vm::jit::x64 {

enum class ArchExitReason {
  UnalignedPc,
  OutOfBoundsPc,
  InstructionFetchFault,
  BlockNotGenerated,
  SingleStep,
  UndefinedInstruction,
  UnsupportedInstruction,
  MemoryReadFault,
  MemoryWriteFault,
  Ecall,
  Ebreak,
};

}  // namespace vm::jit::x64