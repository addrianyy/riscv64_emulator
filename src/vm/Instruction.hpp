#pragma once
#include <cstdint>

#include <fmt/format.h>

#include "Register.hpp"

namespace vm {

enum class InstructionType {
  Undefined = 0,

  Lui,
  Auipc,

  Jal,
  Jalr,

  Beq,
  Bne,
  Blt,
  Bge,
  Bltu,
  Bgeu,

  Lb,
  Lh,
  Lw,
  Ld,
  Lbu,
  Lhu,
  Lwu,

  Sb,
  Sh,
  Sw,
  Sd,

  Addi,
  Xori,
  Ori,
  Andi,
  Addiw,
  Slli,
  Srli,
  Srai,
  Slliw,
  Srliw,
  Sraiw,

  Slti,
  Sltiu,

  Slt,
  Sltu,

  Add,
  Sub,
  Xor,
  Or,
  And,
  Sll,
  Srl,
  Sra,
  Addw,
  Subw,
  Sllw,
  Srlw,
  Sraw,

  Ebreak,
  Ecall,

  Fence,

  Mul,
  Mulw,

  Mulh,
  Mulhu,
  Mulhsu,

  Div,
  Divu,
  Divw,
  Divuw,

  Rem,
  Remu,
  Remw,
  Remuw,
};

class Instruction {
  friend struct InstructionDecoder;

  uint32_t a{};
  uint32_t b{};

 public:
  explicit Instruction(uint32_t encoded_instruction);

  InstructionType type() const { return InstructionType(a & 0xffff); }

  Register rd() const { return Register((a >> 16) & 0b11111); }
  Register rs1() const { return Register((a >> 21) & 0b11111); }
  Register rs2() const { return Register((a >> 26) & 0b11111); }

  int64_t imm() const { return int32_t(b); }
  uint32_t shamt() const { return b; }
};

namespace detail {

std::string_view instruction_name(InstructionType type);
std::string instruction_representation(const Instruction& instruction);

}  // namespace detail

}  // namespace vm

template <>
struct fmt::formatter<vm::InstructionType> : formatter<std::string_view> {
  auto format(vm::InstructionType type, format_context& ctx) const {
    return formatter<string_view>::format(vm::detail::instruction_name(type), ctx);
  }
};

template <>
struct fmt::formatter<vm::Instruction> : formatter<std::string_view> {
  auto format(const vm::Instruction& instruction, format_context& ctx) const {
    return formatter<string_view>::format(vm::detail::instruction_representation(instruction), ctx);
  }
};