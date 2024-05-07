#pragma once
#include <span>

#include <vm/Instruction.hpp>

namespace vm::jit::utils {

template <typename T>
std::span<const uint8_t> cast_to_bytes(std::span<const T> data) {
  return std::span{reinterpret_cast<const uint8_t*>(data.data()), data.size() * sizeof(T)};
}

uint64_t memory_access_size_log2(InstructionType type);

}  // namespace vm::jit::utils