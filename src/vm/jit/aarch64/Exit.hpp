#pragma once

namespace vm::jit::aarch64 {

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

}