#pragma once
#include <vm/jit/CodeBuffer.hpp>

#include <cstdint>

#include "CodegenContext.hpp"

namespace vm::jit::aarch64 {

struct TrampolineBlock {
  uint64_t register_state;
  uint64_t memory_base;
  uint64_t memory_size;
  uint64_t block_base;
  uint64_t max_executable_pc;
  uint64_t code_base;
  uint64_t entrypoint;

  uint64_t exit_reason;
  uint64_t exit_pc;
};

void* generate_trampoline(CodegenContext& context, CodeBuffer& code_buffer);

}  // namespace vm::jit::aarch64