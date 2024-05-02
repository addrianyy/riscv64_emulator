#include "Interpreter.hpp"
#include "Instruction.hpp"

#include <base/Error.hpp>

using namespace vm;

static uint64_t signextend32(uint64_t value) {
  return uint64_t(int64_t(int32_t(value)));
}

bool Interpreter::step(Memory& memory, Cpu& cpu, Exit& exit) {
  const auto current_pc = cpu.pc();
  if ((current_pc & 3) != 0) {
    exit.reason = Exit::Reason::UnalignedPc;
    return false;
  }

  uint32_t encoded_instruction;
  if (!memory.read<uint32_t>(current_pc, encoded_instruction)) {
    exit.reason = Exit::Reason::InstructionFetchFault;
    return false;
  }

  uint64_t next_pc = current_pc + 4;

  const Instruction instruction(encoded_instruction);
  const auto instruction_type = instruction.type();

  using IT = InstructionType;

  switch (instruction_type) {
    case IT::Lui: {
      cpu.set_reg(instruction.rd(), instruction.imm());
      break;
    }

    case IT::Auipc: {
      cpu.set_reg(instruction.rd(), current_pc + instruction.imm());
      break;
    }

    case IT::Jal: {
      const auto target = current_pc + instruction.imm();

      cpu.set_reg(instruction.rd(), next_pc);
      next_pc = target;

      break;
    }

    case IT::Jalr: {
      const auto target = (cpu.reg(instruction.rs1()) + instruction.imm()) & ~uint64_t(1);

      cpu.set_reg(instruction.rd(), next_pc);
      next_pc = target;

      break;
    }

    case IT::Beq:
    case IT::Bne:
    case IT::Blt:
    case IT::Bge:
    case IT::Bltu:
    case IT::Bgeu: {
      const auto a = cpu.reg(instruction.rs1());
      const auto b = cpu.reg(instruction.rs2());

      bool result = false;

      switch (instruction_type) {
          // clang-format off
        case IT::Beq:  result = a == b; break;
        case IT::Bne:  result = a != b; break;
        case IT::Blt:  result = int64_t(a) < int64_t(b); break;
        case IT::Bge:  result = int64_t(a) >= int64_t(b); break;
        case IT::Bltu: result = a < b; break;
        case IT::Bgeu: result = a >= b; break;
          // clang-format on

        default:
          unreachable();
      }

      if (result) {
        const auto target = current_pc + instruction.imm();
        next_pc = target;
      }

      break;
    }

    case IT::Lb:
    case IT::Lh:
    case IT::Lw:
    case IT::Ld:
    case IT::Lbu:
    case IT::Lhu:
    case IT::Lwu: {
      const auto address = cpu.reg(instruction.rs1()) + instruction.imm();

      bool success = false;
      uint64_t result = 0;

#define CASE(instruction, type)              \
  case IT::instruction: {                    \
    type v{};                                \
    success = memory.read<type>(address, v); \
    result = uint64_t(v);                    \
    break;                                   \
  }

      switch (instruction_type) {
        CASE(Lb, int8_t)
        CASE(Lh, int16_t)
        CASE(Lw, int32_t)
        CASE(Ld, int64_t)
        CASE(Lbu, uint8_t)
        CASE(Lhu, uint16_t)
        CASE(Lwu, uint32_t)

        default:
          unreachable();
      }

#undef CASE

      if (!success) {
        exit.reason = Exit::Reason::MemoryReadFault;
        exit.faulty_address = address;
        exit.target_register = instruction.rd();

        return false;
      }

      cpu.set_reg(instruction.rd(), result);

      break;
    }

    case IT::Sb:
    case IT::Sh:
    case IT::Sw:
    case IT::Sd: {
      const auto address = cpu.reg(instruction.rs1()) + instruction.imm();
      const auto value = cpu.reg(instruction.rs2());

      bool success = false;

#define CASE(instruction, type)                   \
  case IT::instruction: {                         \
    success = memory.write<type>(address, value); \
    break;                                        \
  }

      switch (instruction_type) {
        CASE(Sb, uint8_t)
        CASE(Sh, uint16_t)
        CASE(Sw, uint32_t)
        CASE(Sd, uint64_t)

        default:
          unreachable();
      }

#undef CASE

      if (!success) {
        exit.reason = Exit::Reason::MemoryWriteFault;
        exit.faulty_address = address;
        exit.target_register = instruction.rs2();

        return false;
      }

      break;
    }

    case IT::Addi:
    case IT::Xori:
    case IT::Ori:
    case IT::Andi:
    case IT::Addiw: {
      const auto a = cpu.reg(instruction.rs1());
      const auto b = instruction.imm();

      uint64_t result = 0;

      switch (instruction_type) {
          // clang-format off
        case IT::Addi:  result = a + b; break;
        case IT::Xori:  result = a ^ b; break;
        case IT::Ori:   result = a | b; break;
        case IT::Andi:  result = a & b; break;
        case IT::Addiw: result = signextend32(a + b); break;
          // clang-format on

        default:
          unreachable();
      }

      cpu.set_reg(instruction.rd(), result);

      break;
    }

    case IT::Slli:
    case IT::Srli:
    case IT::Srai:
    case IT::Slliw:
    case IT::Srliw:
    case IT::Sraiw: {
      const auto a = cpu.reg(instruction.rs1());
      const auto shamt = instruction.shamt();

      uint64_t result = 0;

      switch (instruction_type) {
          // clang-format off
        case IT::Slli:  result = a << shamt; break;
        case IT::Srli:  result = a >> shamt; break;
        case IT::Srai:  result = uint64_t(int64_t(a) << shamt); break;
        case IT::Slliw: result = signextend32(uint32_t(a) << shamt); break;
        case IT::Srliw: result = signextend32(uint32_t(a) >> shamt); break;
        case IT::Sraiw: result = signextend32(int32_t(a) >> shamt); break;
          // clang-format on

        default:
          unreachable();
      }

      cpu.set_reg(instruction.rd(), result);

      break;
    }

    case IT::Slt:
    case IT::Sltu:
    case IT::Slti:
    case IT::Sltiu: {
      const auto a = cpu.reg(instruction.rs1());
      const auto b = (instruction_type == IT::Slt || instruction_type == IT::Sltu)
                       ? cpu.reg(instruction.rs2())
                       : instruction.imm();

      bool result = false;

      if (instruction_type == IT::Slt || instruction_type == IT::Slti) {
        result = int64_t(a) < int64_t(b);
      } else {
        result = a < b;
      }

      cpu.set_reg(instruction.rd(), result);

      break;
    }

    case IT::Add:
    case IT::Sub:
    case IT::Xor:
    case IT::Or:
    case IT::And:
    case IT::Sll:
    case IT::Srl:
    case IT::Sra:
    case IT::Addw:
    case IT::Subw:
    case IT::Sllw:
    case IT::Srlw:
    case IT::Sraw: {
      const auto a = cpu.reg(instruction.rs1());
      const auto b = cpu.reg(instruction.rs2());

      const auto shamt64 = uint32_t(b & 63);
      const auto shamt32 = uint32_t(b & 31);

      uint64_t result = 0;

      switch (instruction_type) {
          // clang-format off
        case IT::Add: result = a + b; break;
        case IT::Sub: result = a - b; break;
        case IT::Xor: result = a ^ b; break;
        case IT::Or:  result = a | b; break;
        case IT::And: result = a & b; break;
        case IT::Sll: result = a << shamt64; break;
        case IT::Srl: result = a >> shamt64; break;
        case IT::Sra: result = uint64_t(int64_t(a) << shamt64); break;
        case IT::Addw: result = signextend32(uint32_t(a) + uint32_t(b)); break;
        case IT::Subw: result = signextend32(uint32_t(a) - uint32_t(b)); break;
        case IT::Sllw: result = signextend32(uint32_t(a) << shamt32); break;
        case IT::Srlw: result = signextend32(uint32_t(a) >> shamt32); break;
        case IT::Sraw: result = signextend32(int32_t(a) >> shamt32); break;
          // clang-format on

        default:
          unreachable();
      }

      cpu.set_reg(instruction.rd(), result);

      break;
    }

    case IT::Mul:
    case IT::Mulw:
    case IT::Div:
    case IT::Divw:
    case IT::Divu:
    case IT::Divuw:
    case IT::Rem:
    case IT::Remu:
    case IT::Remw:
    case IT::Remuw: {
      const auto a = cpu.reg(instruction.rs1());
      const auto b = cpu.reg(instruction.rs2());

      uint64_t result = 0;

      switch (instruction_type) {
          // clang-format off
        case IT::Mul:   result = a * b; break;
        case IT::Mulw:  result = signextend32(uint32_t(a) / uint32_t(b)); break;
        case IT::Div:   result = int64_t(a) / int64_t(b); break;
        case IT::Divw:  result = signextend32(int32_t(a) / int32_t(b)); break;
        case IT::Divu:  result = uint64_t(a) / uint64_t(b); break;
        case IT::Divuw: result = signextend32(uint32_t(a) / uint32_t(b)); break;
        case IT::Rem:   result = int64_t(a) % int64_t(b); break;
        case IT::Remu:  result = uint64_t(a) % uint64_t(b); break;
        case IT::Remw:  result = signextend32(int32_t(a) % int32_t(b)); break;
        case IT::Remuw: result = signextend32(uint32_t(a) % uint32_t(b)); break;
          // clang-format on

        default:
          unreachable();
      }

      cpu.set_reg(instruction.rd(), result);

      break;
    }

    case IT::Mulh:
    case IT::Mulhu:
    case IT::Mulhsu: {
      fatal_error("multiply high instructions are not supported");
    }

    case IT::Ecall: {
      exit.reason = Exit::Reason::Ecall;
      return false;
    }

    case IT::Ebreak: {
      exit.reason = Exit::Reason::Ebreak;
      return false;
    }

    case IT::Undefined: {
      exit.reason = Exit::Reason::UndefinedInstruction;
      return false;
    }

    case IT::Fence: {
      break;
    }

    default:
      unreachable();
  }

  cpu.set_reg(Register::Pc, next_pc);

  return true;
}
