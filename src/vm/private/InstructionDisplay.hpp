#pragma once
#include <string_view>

#include <vm/Instruction.hpp>

namespace vm {

struct InstructionDisplay {
  enum class Format {
    Standalone,

    Store,
    Load,

    RdImm,
    RdRs1Imm,
    Rs1Rs2Imm,
    RdRs1Rs2,
  };

  static std::string_view instruction_name(InstructionType type);
  static std::string_view register_name(Register reg);
  static Format instruction_format(InstructionType type);

  static void format_instruction(const Instruction& instruction, std::string& formatted);
  static std::string format_instruction(const Instruction& instruction);
};

}  // namespace vm
