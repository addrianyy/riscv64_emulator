#pragma once
#include <span>
#include <vm/Instruction.hpp>

namespace vm::jit::aarch64::utils {

std::span<const uint8_t> cast_instructions_to_bytes(std::span<const uint32_t> instructions);
uint64_t memory_access_size_log2(InstructionType type);

}  // namespace vm::jit::aarch64::utils