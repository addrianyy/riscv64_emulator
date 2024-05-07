#pragma once
#include <vm/Memory.hpp>
#include <vm/jit/CodeBuffer.hpp>

#include "CodegenContext.hpp"

namespace vm::jit::x64 {

std::span<const uint8_t> generate_block_code(CodegenContext& context,
                                             const CodeBuffer& code_buffer,
                                             const Memory& memory,
                                             bool single_step,
                                             uint64_t pc);

}  // namespace vm::jit::x64