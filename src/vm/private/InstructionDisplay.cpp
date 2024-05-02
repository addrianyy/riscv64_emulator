#include "InstructionDisplay.hpp"

#include <base/Error.hpp>

using namespace vm;

std::string_view InstructionDisplay::instruction_name(InstructionType type) {
  switch (type) {
#define CASE(type, name)      \
  case InstructionType::type: \
    return name;

    CASE(Undefined, "undefined")
    CASE(Lui, "lui")
    CASE(Auipc, "auipc")
    CASE(Jal, "jal")
    CASE(Jalr, "jalr")
    CASE(Beq, "beq")
    CASE(Bne, "bne")
    CASE(Blt, "blt")
    CASE(Bge, "bge")
    CASE(Bltu, "bltu")
    CASE(Bgeu, "bgeu")
    CASE(Lb, "lb")
    CASE(Lh, "lh")
    CASE(Lw, "lw")
    CASE(Ld, "ld")
    CASE(Lbu, "lbu")
    CASE(Lhu, "lhu")
    CASE(Lwu, "lwu")
    CASE(Sb, "sb")
    CASE(Sh, "sh")
    CASE(Sw, "sw")
    CASE(Sd, "sd")
    CASE(Addi, "addi")
    CASE(Xori, "xori")
    CASE(Ori, "ori")
    CASE(Andi, "andi")
    CASE(Addiw, "addiw")
    CASE(Slli, "slli")
    CASE(Srli, "srli")
    CASE(Srai, "srai")
    CASE(Slliw, "slliw")
    CASE(Srliw, "srliw")
    CASE(Sraiw, "sraiw")
    CASE(Slti, "slti")
    CASE(Sltiu, "sltiu")
    CASE(Slt, "slt")
    CASE(Sltu, "sltu")
    CASE(Add, "add")
    CASE(Sub, "sub")
    CASE(Xor, "xor")
    CASE(Or, "or")
    CASE(And, "and")
    CASE(Sll, "sll")
    CASE(Srl, "srl")
    CASE(Sra, "sra")
    CASE(Addw, "addw")
    CASE(Subw, "subw")
    CASE(Sllw, "sllw")
    CASE(Srlw, "srlw")
    CASE(Sraw, "sraw")
    CASE(Ebreak, "ebreak")
    CASE(Ecall, "ecall")
    CASE(Fence, "fence")
    CASE(Mul, "mul")
    CASE(Mulw, "mulw")
    CASE(Mulh, "mulh")
    CASE(Mulhu, "mulhu")
    CASE(Mulhsu, "mulhsu")
    CASE(Div, "div")
    CASE(Divu, "divu")
    CASE(Divw, "divw")
    CASE(Divuw, "divuw")
    CASE(Rem, "rem")
    CASE(Remu, "remu")
    CASE(Remw, "remw")
    CASE(Remuw, "remuw")

#undef CASE

    default:
      unreachable();
  }
}

std::string_view InstructionDisplay::register_name(Register reg) {
  switch (reg) {
#define CASE(type, name) \
  case Register::type:   \
    return name;

    CASE(Zero, "zero")
    CASE(Ra, "ra")
    CASE(Sp, "sp")
    CASE(Gp, "gp")
    CASE(Tp, "tp")
    CASE(T0, "t0")
    CASE(T1, "t1")
    CASE(T2, "t2")
    CASE(S0, "s0")
    CASE(S1, "s1")
    CASE(A0, "a0")
    CASE(A1, "a1")
    CASE(A2, "a2")
    CASE(A3, "a3")
    CASE(A4, "a4")
    CASE(A5, "a5")
    CASE(A6, "a6")
    CASE(A7, "a7")
    CASE(S2, "s2")
    CASE(S3, "s3")
    CASE(S4, "s4")
    CASE(S5, "s5")
    CASE(S6, "s6")
    CASE(S7, "s7")
    CASE(S8, "s8")
    CASE(S9, "s9")
    CASE(S10, "s10")
    CASE(S11, "s11")
    CASE(T3, "t3")
    CASE(T4, "t4")
    CASE(T5, "t5")
    CASE(T6, "t6")
    CASE(Pc, "pc")

#undef CASE

    default:
      unreachable();
  }
}

static bool instruction_between(InstructionType type, InstructionType first, InstructionType last) {
  return uint32_t(type) >= uint32_t(first) && uint32_t(type) <= uint32_t(last);
}

InstructionDisplay::Format InstructionDisplay::instruction_format(InstructionType type) {
  if (type == InstructionType::Undefined) {
    return Format::Standalone;
  }

  if (instruction_between(type, InstructionType::Lui, InstructionType::Jal)) {
    return Format::RdImm;
  }

  if (type == InstructionType::Jalr) {
    return Format::RdRs1Imm;
  }

  if (instruction_between(type, InstructionType::Beq, InstructionType::Bgeu)) {
    return Format::Rs1Rs2Imm;
  }

  if (instruction_between(type, InstructionType::Lb, InstructionType::Lwu)) {
    return Format::Load;
  }

  if (instruction_between(type, InstructionType::Sb, InstructionType::Sd)) {
    return Format::Store;
  }

  if (instruction_between(type, InstructionType::Addi, InstructionType::Sltiu)) {
    return Format::RdRs1Imm;
  }

  if (instruction_between(type, InstructionType::Slt, InstructionType::Sraw)) {
    return Format::RdRs1Rs2;
  }

  if (instruction_between(type, InstructionType::Ebreak, InstructionType::Ecall)) {
    return Format::Standalone;
  }

  if (type == InstructionType::Fence) {
    return Format::RdRs1Imm;
  }

  if (instruction_between(type, InstructionType::Mul, InstructionType::Remuw)) {
    return Format::RdRs1Rs2;
  }

  unreachable();
}

void InstructionDisplay::format_instruction(const Instruction& instruction,
                                            std::string& formatted) {
  const auto name = instruction_name(instruction.type());
  const auto format = instruction_format(instruction.type());

  auto inserter = std::back_inserter(formatted);

  switch (format) {
    case Format::Standalone: {
      formatted = name;
      break;
    }

    case Format::Store: {
      base::format_to(inserter, "{} {}, {:#x}({})", name, instruction.rs2(), instruction.imm(),
                      instruction.rs1());
      break;
    }

    case Format::Load: {
      base::format_to(inserter, "{} {}, {:#x}({})", name, instruction.rd(), instruction.imm(),
                      instruction.rs1());
      break;
    }

    case Format::RdImm: {
      base::format_to(inserter, "{} {}, {:#x}", name, instruction.rd(), instruction.imm());
      break;
    }

    case Format::RdRs1Imm: {
      base::format_to(inserter, "{} {}, {}, {:#x}", name, instruction.rd(), instruction.rs1(),
                      instruction.imm());
      break;
    }

    case Format::Rs1Rs2Imm: {
      base::format_to(inserter, "{} {}, {}, {:#x}", name, instruction.rs1(), instruction.rs2(),
                      instruction.imm());
      break;
    }

    case Format::RdRs1Rs2: {
      base::format_to(inserter, "{} {}, {}, {}", name, instruction.rd(), instruction.rs1(),
                      instruction.rs2());
      break;
    }

    default:
      unreachable();
  }
}

std::string InstructionDisplay::format_instruction(const Instruction& instruction) {
  std::string formatted;
  format_instruction(instruction, formatted);
  return formatted;
}
