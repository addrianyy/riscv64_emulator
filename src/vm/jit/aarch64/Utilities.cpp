#include "Utilities.hpp"

#include <base/Error.hpp>

using namespace vm::jit::aarch64;

std::span<const uint8_t> utils::cast_instructions_to_bytes(std::span<const uint32_t> instructions) {
  return std::span{reinterpret_cast<const uint8_t*>(instructions.data()),
                   instructions.size() * sizeof(uint32_t)};
}

uint64_t utils::memory_access_size_log2(InstructionType type) {
  using IT = InstructionType;

  switch (type) {
    case IT::Sb:
    case IT::Lb:
    case IT::Lbu:
      return 0;

    case IT::Sh:
    case IT::Lh:
    case IT::Lhu:
      return 1;

    case IT::Sw:
    case IT::Lw:
    case IT::Lwu:
      return 2;

    case IT::Sd:
    case IT::Ld:
      return 3;

    default:
      unreachable();
  }
}