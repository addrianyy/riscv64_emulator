#include "Instruction.hpp"
#include "private/InstructionDisplay.hpp"

#include <base/Error.hpp>

using namespace vm;

static uint32_t sign32(uint32_t value) {
  return uint32_t(int32_t(value & 0x8000'0000) >> 31);
}

namespace vm {
struct InstructionDecoder {
  uint32_t instruction;
  Instruction& decoded_instruction;

  void set_decoded(InstructionType type, uint32_t rd, uint32_t rs1, uint32_t rs2, uint32_t imm) {
    decoded_instruction.a = uint32_t(type) | (rd << 16) | (rs1 << 21) | (rs2 << 26);
    decoded_instruction.b = imm;
  }

  void decode_itype(uint32_t opcode) {
    const auto imm0_10 = (instruction >> 20) & 0b111'1111'1111;
    const auto imm11_31 = sign32(instruction);
    const auto imm = (imm0_10 | (imm11_31 << 11));

    const auto rd = (instruction >> 7) & 0b11111;
    const auto rs1 = (instruction >> 15) & 0b11111;

    const auto funct3 = (instruction >> 12) & 0b111;

    const auto shamt = (instruction >> 20) & 0b11'1111;
    const auto shtype = (instruction >> 26) & 0b11'1111;
    const auto shamt32 = (instruction >> 20) & 0b1'1111;
    const auto shtype32 = (instruction >> 25) & 0b11'11111;

    switch (opcode) {
      case 0b001'0011: {
        const auto decoded = [this, rd, rs1](InstructionType type, uint32_t imm) {
          return set_decoded(type, rd, rs1, 0, imm);
        };

        switch (funct3) {
            // clang-format off
          case 0b000: return decoded(InstructionType::Addi, imm);
          case 0b010: return decoded(InstructionType::Slti, imm);
          case 0b011: return decoded(InstructionType::Sltiu, imm);
          case 0b100: return decoded(InstructionType::Xori, imm);
          case 0b110: return decoded(InstructionType::Ori, imm);
          case 0b111: return decoded(InstructionType::Andi, imm);
            // clang-format on

          case 0b001: {
            if (shtype == 0b00'0000) {
              return decoded(InstructionType::Slli, shamt);
            }
            break;
          }

          case 0b101: {
            if (shtype == 0b00'0000 || shtype == 0b01'0000) {
              return decoded(shtype == 0b00'0000 ? InstructionType::Srli : InstructionType::Srai,
                             shamt);
            }
            break;
          }

          default:
            break;
        }
      }

      case 0b000'0011: {
        const auto decoded = [this, rd, rs1, imm](InstructionType type) {
          return set_decoded(type, rd, rs1, 0, imm);
        };

        switch (funct3) {
            // clang-format off
            case 0b000: return decoded(InstructionType::Lb);
            case 0b001: return decoded(InstructionType::Lh);
            case 0b010: return decoded(InstructionType::Lw);
            case 0b100: return decoded(InstructionType::Lbu);
            case 0b101: return decoded(InstructionType::Lhu);
            case 0b110: return decoded(InstructionType::Lwu);
            case 0b011: return decoded(InstructionType::Ld);
            // clang-format on

          default:
            break;
        }
      }

      case 0b001'1011: {
        switch (funct3) {
          case 0b000: {
            return set_decoded(InstructionType::Addiw, rd, rs1, 0, imm);
          }

          case 0b001: {
            if (shtype32 == 0b000'0000) {
              return set_decoded(InstructionType::Slliw, rd, rs1, 0, shamt32);
            }
            break;
          }

          case 0b101: {
            if (shtype32 == 0b000'0000 || shtype == 0b010'0000) {
              return set_decoded(
                shtype32 == 0b000'0000 ? InstructionType::Srliw : InstructionType::Sraiw, rd, rs1,
                0, shamt32);
            }
            break;
          }

          default:
            break;
        }

        break;
      }

      case 0b111'0011: {
        if (funct3 == 0 && rs1 == 0 && rd == 0) {
          if (imm == 0 || imm == 1) {
            return set_decoded(imm == 0 ? InstructionType::Ecall : InstructionType::Ebreak, 0, 0, 0,
                               0);
          }
        }
        break;
      }

      case 0b000'1111: {
        if (funct3 == 0) {
          return set_decoded(InstructionType::Fence, rd, rs1, 0, imm);
        }
        break;
      }

      case 0b110'0111: {
        if (funct3 == 0) {
          return set_decoded(InstructionType::Jalr, rd, rs1, 0, imm);
        }
        break;
      }

      default:
        break;
    }
  }

  void decode_utype(uint32_t opcode) {
    const auto imm = instruction & 0xffff'f000;
    const auto rd = (instruction >> 7) & 0b11111;

    switch (opcode) {
      case 0b011'0111: {
        return set_decoded(InstructionType::Lui, rd, 0, 0, imm);
      }

      case 0b001'0111: {
        return set_decoded(InstructionType::Auipc, rd, 0, 0, imm);
      }

      default:
        break;
    }
  }

  void decode_rtype(uint32_t opcode) {
    const auto rd = (instruction >> 7) & 0b11111;
    const auto rs1 = (instruction >> 15) & 0b11111;
    const auto rs2 = (instruction >> 20) & 0b11111;

    const auto funct3 = (instruction >> 12) & 0b111;
    const auto funct7 = (instruction >> 25) & 0b111'1111;

    const auto decoded = [this, rs1, rs2, rd](InstructionType type) {
      return set_decoded(type, rd, rs1, rs2, 0);
    };

    switch (opcode) {
      case 0b011'0011: {
        switch (funct7) {
          case 0b000'0000: {
            switch (funct3) {
                // clang-format off
              case 0b000: return decoded(InstructionType::Add);
              case 0b001: return decoded(InstructionType::Sll);
              case 0b010: return decoded(InstructionType::Slt);
              case 0b011: return decoded(InstructionType::Sltu);
              case 0b100: return decoded(InstructionType::Xor);
              case 0b101: return decoded(InstructionType::Srl);
              case 0b110: return decoded(InstructionType::Or);
              case 0b111: return decoded(InstructionType::And);
                // clang-format on
              default:
                break;
            }
          }

          case 0b010'0000: {
            switch (funct3) {
                // clang-format off
              case 0b000: return decoded(InstructionType::Sub);
              case 0b101: return decoded(InstructionType::Sra);
                // clang-format on
              default:
                break;
            }
            break;
          }

          case 0b000'0001: {
            switch (funct3) {
                // clang-format off
              case 0b000: return decoded(InstructionType::Mul);
              case 0b001: return decoded(InstructionType::Mulh);
              case 0b010: return decoded(InstructionType::Mulhsu);
              case 0b011: return decoded(InstructionType::Mulhu);
              case 0b100: return decoded(InstructionType::Div);
              case 0b101: return decoded(InstructionType::Divu);
              case 0b110: return decoded(InstructionType::Rem);
              case 0b111: return decoded(InstructionType::Remu);
                // clang-format on
              default:
                break;
            }
            break;
          }

          default:
            break;
        }

        break;
      }

      case 0b011'1011: {
        switch (funct7) {
          case 0b000'0000: {
            switch (funct3) {
                // clang-format off
              case 0b000: return decoded(InstructionType::Addw);
              case 0b001: return decoded(InstructionType::Sllw);
              case 0b101: return decoded(InstructionType::Srlw);
                // clang-format on
              default:
                break;
            }
            break;
          }

          case 0b010'0000: {
            switch (funct3) {
                // clang-format off
              case 0b000: return decoded(InstructionType::Subw);
              case 0b101: return decoded(InstructionType::Sraw);
                // clang-format on
              default:
                break;
            }
            break;
          }

          case 0b000'0001: {
            switch (funct3) {
                // clang-format off
              case 0b000: return decoded(InstructionType::Mulw);
              case 0b100: return decoded(InstructionType::Divw);
              case 0b101: return decoded(InstructionType::Divuw);
              case 0b110: return decoded(InstructionType::Remw);
              case 0b111: return decoded(InstructionType::Remuw);
                // clang-format on
              default:
                break;
            }
            break;
          }

          default:
            break;
        }
      }

      default:
        break;
    }
  }

  void decode_stype(uint32_t opcode) {
    const auto imm0_4 = (instruction >> 7) & 0b11111;
    const auto imm5_10 = (instruction >> 25) & 0b11'1111;
    const auto imm11_31 = sign32(instruction);
    const auto imm = (imm0_4 | (imm5_10 << 5) | (imm11_31 << 11));

    const auto rs1 = (instruction >> 15) & 0b11111;
    const auto rs2 = (instruction >> 20) & 0b11111;

    const auto funct3 = (instruction >> 12) & 0b111;

    if (opcode == 0b010'0011) {
      const auto decoded = [this, rs1, rs2, imm](InstructionType type) {
        return set_decoded(type, 0, rs1, rs2, imm);
      };

      switch (funct3) {
          // clang-format off
        case 0b000: return decoded(InstructionType::Sb);
        case 0b001: return decoded(InstructionType::Sh);
        case 0b010: return decoded(InstructionType::Sw);
        case 0b011: return decoded(InstructionType::Sd);
          // clang-format on

        default:
          break;
      }
    }
  }

  void decode_btype(uint32_t opcode) {
    const auto imm1_4 = (instruction >> 8) & 0b1111;
    const auto imm5_10 = (instruction >> 25) & 0b11'1111;
    const auto imm11 = (instruction >> 7) & 0b1;
    const auto imm12_31 = sign32(instruction);
    const auto imm = ((imm1_4 << 1) | (imm5_10 << 5) | (imm11 << 11) | (imm12_31 << 12));

    const auto rs1 = (instruction >> 15) & 0b11111;
    const auto rs2 = (instruction >> 20) & 0b11111;

    const auto funct3 = (instruction >> 12) & 0b111;

    if (opcode == 0b110'0011) {
      const auto decoded = [this, rs1, rs2, imm](InstructionType type) {
        return set_decoded(type, 0, rs1, rs2, imm);
      };

      switch (funct3) {
          // clang-format off
        case 0b000: return decoded(InstructionType::Beq);
        case 0b001: return decoded(InstructionType::Bne);
        case 0b100: return decoded(InstructionType::Blt);
        case 0b101: return decoded(InstructionType::Bge);
        case 0b110: return decoded(InstructionType::Bltu);
        case 0b111: return decoded(InstructionType::Bgeu);
          // clang-format on

        default:
          break;
      }
    }
  }

  void decode_jtype(uint32_t opcode) {
    const auto imm1_10 = (instruction >> 21) & 0b11'1111'1111;
    const auto imm11 = (instruction >> 20) & 0b1;
    const auto imm12_19 = (instruction >> 12) & 0b1111'1111;
    const auto imm20_31 = sign32(instruction);
    const auto imm = ((imm1_10 << 1) | (imm11 << 11) | (imm12_19 << 12) | (imm20_31 << 20));

    const auto rd = (instruction >> 7) & 0b11111;

    if (opcode == 0b1101111) {
      return set_decoded(InstructionType::Jal, rd, 0, 0, imm);
    }
  }

  void decode() {
    set_decoded(InstructionType::Undefined, 0, 0, 0, 0);

    const auto opcode = instruction & 0b111'1111;

    switch (opcode) {
      case 0b0000011:
      case 0b0001111:
      case 0b0010011:
      case 0b0011011:
      case 0b1110011:
      case 0b1100111:
        return decode_itype(opcode);

      case 0b0010111:
      case 0b0110111:
        return decode_utype(opcode);

      case 0b0110011:
      case 0b0111011:
        return decode_rtype(opcode);

      case 0b0100011:
        return decode_stype(opcode);

      case 0b1100011:
        return decode_btype(opcode);

      case 0b1101111:
        return decode_jtype(opcode);

      default:
        break;
    }
  }
};
}  // namespace vm

Instruction::Instruction(uint32_t encoded_instruction) {
  InstructionDecoder{encoded_instruction, *this}.decode();
}

std::string_view detail::instruction_name(InstructionType type) {
  return InstructionDisplay::instruction_name(type);
}

std::string detail::instruction_representation(const Instruction& instruction) {
  return InstructionDisplay::format_instruction(instruction);
}