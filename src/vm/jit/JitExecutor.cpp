#include "JitExecutor.hpp"
#include "JitCodeDump.hpp"

#include <base/Error.hpp>
#include <base/Log.hpp>

#include <vm/Instruction.hpp>
#include <vm/private/ExecutionLog.hpp>

#include <asmlib_a64/Assembler.hpp>

using namespace vm;

using A64R = a64::Register;

enum class JitExitReason {
  UnalignedPc,
  InstructionFetchFault,
  UndefinedInstruction,
  UnsupportedInstruction,
  MemoryReadFault,
  MemoryWriteFault,
  BlockNotGenerated,
  SingleStep,
  BlockOutOfBounds,
  DynamicBranchFailed,
  Ecall,
  Ebreak,
};

struct JitTrampolineBlock {
  uint64_t register_state;
  uint64_t memory_base;
  uint64_t memory_size;
  uint64_t block_base;
  uint64_t max_executable_pc;
  uint64_t code_base;
  uint64_t outline_handlers;
  uint64_t entrypoint;

  uint64_t exit_reason;
  uint64_t exit_pc;
};

static std::span<const uint8_t> cast_instructions_to_bytes(std::span<const uint32_t> instructions) {
  return std::span{reinterpret_cast<const uint8_t*>(instructions.data()),
                   instructions.size() * sizeof(uint32_t)};
}

static uint64_t memory_access_size_log2(InstructionType type) {
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

struct TemplateGenerator {
  constexpr static A64R register_state_reg = A64R::X0;
  constexpr static A64R memory_base_reg = A64R::X1;
  constexpr static A64R memory_size_reg = A64R::X2;
  constexpr static A64R block_base_reg = A64R::X3;
  constexpr static A64R max_executable_pc_reg = A64R::X4;
  constexpr static A64R code_base_reg = A64R::X5;
  constexpr static A64R base_pc_reg = A64R::X6;

  constexpr static A64R exit_reason_reg = A64R::X0;
  constexpr static A64R exit_pc_reg = A64R::X1;

  // ABI = X8, X9

  constexpr static A64R outline_handler_pc_reg = A64R::X8;
  constexpr static A64R outline_handler_arg_reg = A64R::X9;

  constexpr static A64R return_address_reg = A64R::X15;

  template <typename Exit>
  static void generate_memory_translate(a64::Assembler& as,
                                        A64R address_reg,
                                        uint64_t access_size_log2,
                                        bool write,
                                        const Exit& generate_exit) {
    const auto fault_label = as.allocate_label();
    const auto continue_label = as.allocate_label();

    // Make sure that address is aligned otherwise the bound check later won't be accurate.
    if (access_size_log2 > 0) {
      as.tst(address_reg, (1 << access_size_log2) - 1);
      as.b(a64::Condition::NotZero, fault_label);
    }

    // Check if address < memory_size. We don't need to account for the access size because we
    // have already checked for alignment.
    as.cmp(address_reg, memory_size_reg);
    as.b(a64::Condition::UnsignedLess, continue_label);

    // Out of bounds access: exit the VM.
    as.insert_label(fault_label);
    generate_exit(write ? JitExitReason::MemoryWriteFault : JitExitReason::MemoryReadFault);

    // Continue the memory access.
    as.insert_label(continue_label);

    as.add(address_reg, memory_base_reg, address_reg);
  }

  template <typename Exit>
  static void generate_validated_branch_noexit(a64::Assembler& as,
                                               A64R block_offset_reg,
                                               bool single_step,
                                               const Exit& generate_exit) {
    // Load the 32 bit code offset from block translation table.
    as.add(block_offset_reg, block_base_reg, block_offset_reg);
    as.ldar(cast_to_32bit(block_offset_reg), block_offset_reg);

    const auto code_offset_reg = block_offset_reg;

    // Exit the VM if the block isn't generated yet.
    const auto no_block_label = as.allocate_label();
    as.cbz(code_offset_reg, no_block_label);

    // Jump to the block.
    as.add(code_offset_reg, code_base_reg, code_offset_reg);
    as.br(code_offset_reg);

    as.insert_label(no_block_label);
    generate_exit(JitExitReason::BlockNotGenerated);
  }

  template <typename Exit>
  static void generate_dynamic_branch(a64::Assembler& as,
                                      A64R target_pc,
                                      A64R scratch_reg,
                                      bool single_step,
                                      const Exit& generate_exit) {
    const auto fault_label = as.allocate_label();

    // Mask off last bit as is required by the architecture.
    as.and_(target_pc, target_pc, ~uint64_t(1));

    // Exit the VM if the address is not properly aligned.
    as.tst(target_pc, 0b11);
    as.b(a64::Condition::NotZero, fault_label);

    // Exit the VM if target_pc >= max_executable_pc.
    as.cmp(target_pc, max_executable_pc_reg);
    as.b(a64::Condition::UnsignedGreaterEqual, fault_label);

    generate_validated_branch_noexit(as, scratch_reg, single_step, generate_exit);
    generate_exit(JitExitReason::BlockNotGenerated);

    as.insert_label(fault_label);
    generate_exit(JitExitReason::DynamicBranchFailed);
  }
};

static void generate_outline_handlers(JitCodeBuffer& code_buffer, OutlineHandlers& handlers) {
  a64::Assembler as;

  for (size_t i = 0; i < 4; ++i) {
    handlers.addresses[size_t(OutlineHandlerID::MemoryTranslate8) + i] = as.byte_offset();

    TemplateGenerator::generate_memory_translate(
      as, TemplateGenerator::outline_handler_arg_reg, i, false, [&](JitExitReason reason) {
        as.macro_mov(TemplateGenerator::exit_reason_reg, int64_t(reason));
        as.mov(TemplateGenerator::exit_pc_reg, TemplateGenerator::outline_handler_pc_reg);
        as.ret(TemplateGenerator::return_address_reg);
      });
    as.ret();
  }

  {
    handlers.addresses[size_t(OutlineHandlerID::StaticBranch)] = as.byte_offset();

    as.mov(TemplateGenerator::outline_handler_arg_reg, TemplateGenerator::outline_handler_pc_reg);
    TemplateGenerator::generate_validated_branch_noexit(
      as, TemplateGenerator::outline_handler_arg_reg, false, [&](JitExitReason reason) {
        as.macro_mov(TemplateGenerator::exit_reason_reg, int64_t(reason));
        as.mov(TemplateGenerator::exit_pc_reg, TemplateGenerator::outline_handler_pc_reg);
        as.ret(TemplateGenerator::return_address_reg);
      });
  }

  {
    handlers.addresses[size_t(OutlineHandlerID::DynamicBranch)] = as.byte_offset();

    as.mov(TemplateGenerator::outline_handler_arg_reg, TemplateGenerator::outline_handler_pc_reg);
    TemplateGenerator::generate_dynamic_branch(
      as, TemplateGenerator::outline_handler_pc_reg, TemplateGenerator::outline_handler_arg_reg,
      false, [&](JitExitReason reason) {
        as.macro_mov(TemplateGenerator::exit_reason_reg, int64_t(reason));
        as.mov(TemplateGenerator::exit_pc_reg, TemplateGenerator::outline_handler_pc_reg);
        as.ret(TemplateGenerator::return_address_reg);
      });
  }

  const auto base_code_address =
    code_buffer.insert_standalone(cast_instructions_to_bytes(as.assembled_instructions()));

  for (size_t i = 0; i < size_t(OutlineHandlerID::Max); ++i) {
    handlers.addresses[i] = uint64_t(base_code_address) + handlers.addresses[i];
  }
}

struct CodeGenerator {
  a64::Assembler& assembler;
  const Memory& memory;

  size_t max_code_blocks{};
  bool single_step{};

  uint64_t base_pc{};
  uint64_t current_pc{};

  constexpr static A64R register_state_reg = A64R::X0;
  constexpr static A64R memory_base_reg = A64R::X1;
  constexpr static A64R memory_size_reg = A64R::X2;
  constexpr static A64R block_base_reg = A64R::X3;
  constexpr static A64R max_executable_pc_reg = A64R::X4;
  constexpr static A64R code_base_reg = A64R::X5;
  constexpr static A64R base_pc_reg = A64R::X6;
  constexpr static A64R outline_handlers_reg = A64R::X7;
  constexpr static A64R return_address_reg = A64R::X15;

  constexpr static A64R exit_reason_reg = A64R::X0;
  constexpr static A64R exit_pc_reg = A64R::X1;

  void load_register(A64R target, Register reg) {
    verify(reg != Register::Pc, "cannot load PC as GPR");

    if (reg == Register::Zero) {
      assembler.mov(target, 0);
    } else {
      assembler.ldr(target, register_state_reg, uint32_t(reg) * sizeof(uint64_t));
    }
  }

  A64R load_register_or_zero(A64R target, Register reg) {
    if (reg == Register::Zero) {
      return A64R::Xzr;
    }

    load_register(target, reg);
    return target;
  }

  void store_register(Register reg, A64R source) {
    verify(reg != Register::Pc, "cannot store PC as GPR");

    if (reg != Register::Zero) {
      assembler.str(source, register_state_reg, uint32_t(reg) * sizeof(uint64_t));
    }
  }

  void load_immediate(A64R target, int64_t immediate) {
    const auto pc_offset = int64_t(immediate) - int64_t(base_pc);
    const auto max_pc_delta = int64_t(std::numeric_limits<int16_t>::max());

    // Roughly estimate if it's worth trying to use pc-based immediates.
    if (pc_offset >= -max_pc_delta && pc_offset <= max_pc_delta) {
      if (assembler.try_add_i(target, base_pc_reg, pc_offset)) {
        return;
      }
    }

    assembler.macro_mov(target, immediate);
  }
  void load_immediate_u(A64R target, uint64_t immediate) {
    return load_immediate(target, int64_t(immediate));
  }

  A64R load_immediate_or_zero(A64R target, int64_t immediate) {
    if (immediate == 0) {
      return A64R::Xzr;
    }

    load_immediate(target, immediate);
    return target;
  }
  A64R load_immediate_or_zero_u(A64R target, uint64_t immediate) {
    return load_immediate_or_zero(target, int64_t(immediate));
  }

  void load_register_with_offset(A64R value_reg, A64R scratch_reg, Register reg, int64_t offset) {
    if (reg == Register::Zero) {
      return load_immediate(value_reg, offset);
    }

    load_register(value_reg, reg);

    if (offset != 0) {
      if (!assembler.try_add_i(value_reg, value_reg, offset)) {
        load_immediate(scratch_reg, offset);
        assembler.add(value_reg, value_reg, scratch_reg);
      }
    }
  }

  void generate_exit(JitExitReason reason) { generate_exit(reason, current_pc); }
  void generate_exit(JitExitReason reason, uint64_t pc) {
    load_immediate_u(exit_reason_reg, uint64_t(reason));
    load_immediate_u(exit_pc_reg, pc);
    assembler.ret(return_address_reg);
  }
  void generate_exit(JitExitReason reason, A64R pc) {
    load_immediate_u(exit_reason_reg, uint64_t(reason));
    assembler.mov(exit_pc_reg, pc);
    assembler.ret(return_address_reg);
  }

  void generate_memory_translate(A64R address_reg, uint64_t access_size_log2, bool write) {
    const auto fault_label = assembler.allocate_label();
    const auto continue_label = assembler.allocate_label();

    // Make sure that address is aligned otherwise the bound check later won't be accurate.
    if (access_size_log2 > 0) {
      assembler.tst(address_reg, (1 << access_size_log2) - 1);
      assembler.b(a64::Condition::NotZero, fault_label);
    }

    // Check if address < memory_size. We don't need to account for the access size because we
    // have already checked for alignment.
    assembler.cmp(address_reg, memory_size_reg);
    assembler.b(a64::Condition::UnsignedLess, continue_label);

    // Out of bounds access: exit the VM.
    assembler.insert_label(fault_label);
    generate_exit(write ? JitExitReason::MemoryWriteFault : JitExitReason::MemoryReadFault);

    // Continue the memory access.
    assembler.insert_label(continue_label);

    assembler.add(address_reg, memory_base_reg, address_reg);
  }

  void generate_validated_branch_noexit(A64R block_offset_reg) {
    // Load the 32 bit code offset from block translation table.
    assembler.add(block_offset_reg, block_base_reg, block_offset_reg);
    assembler.ldar(cast_to_32bit(block_offset_reg), block_offset_reg);

    const auto code_offset_reg = block_offset_reg;

    // Exit the VM if the block isn't generated yet.
    const auto no_block_label = assembler.allocate_label();
    assembler.cbz(code_offset_reg, no_block_label);

    // Jump to the block.
    assembler.add(code_offset_reg, code_base_reg, code_offset_reg);
    assembler.br(code_offset_reg);

    assembler.insert_label(no_block_label);
    // Callee must provide code that handles case where the block is not generated yet.
  }

  void generate_static_branch(uint64_t target_pc, A64R scratch_reg) {
    const auto block = target_pc / 4;

    // We can statically handle some error conditions.
    if ((target_pc & 3) != 0) {
      return generate_exit(JitExitReason::UnalignedPc);
    }
    if (block >= max_code_blocks) {
      return generate_exit(JitExitReason::BlockOutOfBounds);
    }

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(JitExitReason::SingleStep, target_pc);
    } else {
      // Calculate the memory offset from `block_base`.
      load_immediate_u(scratch_reg, block * 4);

      generate_validated_branch_noexit(scratch_reg);
      generate_exit(JitExitReason::BlockNotGenerated, target_pc);
    }
  }

  void generate_dynamic_branch(A64R target_pc, A64R scratch_reg) {
    const auto fault_label = assembler.allocate_label();

    // Mask off last bit as is required by the architecture.
    assembler.and_(target_pc, target_pc, ~uint64_t(1));

    // Exit the VM if the address is not properly aligned.
    assembler.tst(target_pc, 0b11);
    assembler.b(a64::Condition::NotZero, fault_label);

    // Exit the VM if target_pc >= max_executable_pc.
    assembler.cmp(target_pc, max_executable_pc_reg);
    assembler.b(a64::Condition::UnsignedGreaterEqual, fault_label);

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(JitExitReason::SingleStep, target_pc);
    } else {
      generate_validated_branch_noexit(scratch_reg);
      generate_exit(JitExitReason::BlockNotGenerated, target_pc);
    }

    assembler.insert_label(fault_label);
    generate_exit(JitExitReason::DynamicBranchFailed, target_pc);
  }

  bool generate_instruction(const Instruction& instruction) {
    const auto instruction_type = instruction.type();

    using IT = InstructionType;

    switch (instruction_type) {
      case IT::Lui: {
        load_immediate(A64R::X10, instruction.imm());
        store_register(instruction.rd(), A64R::X10);
      }

      case IT::Auipc: {
        load_immediate_u(A64R::X10, current_pc + instruction.imm());
        store_register(instruction.rd(), A64R::X10);
        break;
      }

      case InstructionType::Jal: {
        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          load_immediate_u(A64R::X10, current_pc + 4);
          store_register(rd, A64R::X10);
        }

        const auto target = current_pc + instruction.imm();
        generate_static_branch(target, A64R::X10);

        return false;
      }

      case InstructionType::Jalr: {
        load_register_with_offset(A64R::X10, A64R::X11, instruction.rs1(), instruction.imm());

        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          load_immediate_u(A64R::X11, current_pc + 4);
          store_register(rd, A64R::X11);
        }

        generate_dynamic_branch(A64R::X10, A64R::X11);

        return false;
      }

      case IT::Beq:
      case IT::Bne:
      case IT::Blt:
      case IT::Bge:
      case IT::Bltu:
      case IT::Bgeu: {
        a64::Condition condition;

        // Condition is inverted.
        switch (instruction_type) {
            // clang-format off
          case IT::Beq:  condition = a64::Condition::NotEqual; break;
          case IT::Bne:  condition = a64::Condition::Equal; break;
          case IT::Blt:  condition = a64::Condition::GreaterEqual; break;
          case IT::Bge:  condition = a64::Condition::Less; break;
          case IT::Bltu: condition = a64::Condition::UnsignedGreaterEqual; break;
          case IT::Bgeu: condition = a64::Condition::UnsignedLess; break;
            // clang-format on

          default:
            unreachable();
        }

        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto b = load_register_or_zero(A64R::X11, instruction.rs2());

        const auto skip_label = assembler.allocate_label();

        assembler.cmp(a, b);
        assembler.b(condition, skip_label);

        generate_static_branch(current_pc + instruction.imm(), A64R::X10);

        assembler.insert_label(skip_label);

        break;
      }

      case IT::Lb:
      case IT::Lh:
      case IT::Lw:
      case IT::Ld:
      case IT::Lbu:
      case IT::Lhu:
      case IT::Lwu: {
        const auto address_reg = TemplateGenerator::outline_handler_arg_reg;
        const auto scratch_reg = A64R::X10;
        const auto value_reg = A64R::X10;

        load_register_with_offset(address_reg, scratch_reg, instruction.rs1(), instruction.imm());

        load_immediate_u(TemplateGenerator::outline_handler_pc_reg, current_pc);
        assembler.ldr(A64R::X11, outline_handlers_reg,
                      (uintptr_t(OutlineHandlerID::MemoryTranslate8) +
                       memory_access_size_log2(instruction_type)) *
                        8);
        assembler.blr(A64R::X11);

        // generate_memory_translate(address_reg, memory_access_size_log2(instruction_type),
        // false);

        switch (instruction_type) {
            // clang-format off
          case IT::Lb:  assembler.ldrsb(value_reg, address_reg, 0); break;
          case IT::Lh:  assembler.ldrsh(value_reg, address_reg, 0); break;
          case IT::Lw:  assembler.ldrsw(value_reg, address_reg, 0); break;
          case IT::Ld:  assembler.ldr(value_reg, address_reg, 0); break;
          case IT::Lbu: assembler.ldrb(value_reg, address_reg, 0); break;
          case IT::Lhu: assembler.ldrh(value_reg, address_reg, 0); break;
          case IT::Lwu: assembler.ldr(cast_to_32bit(value_reg), address_reg, 0); break;
            // clang-format on

          default:
            unreachable();
        }

        store_register(instruction.rd(), value_reg);

        break;
      }

      case IT::Sb:
      case IT::Sh:
      case IT::Sw:
      case IT::Sd: {
        const auto address_reg = TemplateGenerator::outline_handler_arg_reg;
        const auto scratch_reg = A64R::X10;
        const auto value_reg = A64R::X10;

        load_register_with_offset(address_reg, scratch_reg, instruction.rs1(), instruction.imm());
        // generate_memory_translate(address_reg, memory_access_size_log2(instruction_type), true);

        load_immediate_u(TemplateGenerator::outline_handler_pc_reg, current_pc);
        assembler.ldr(A64R::X11, outline_handlers_reg,
                      (uintptr_t(OutlineHandlerID::MemoryTranslate8) +
                       memory_access_size_log2(instruction_type)) *
                        8);
        assembler.blr(A64R::X11);

        load_register(value_reg, instruction.rs2());

        switch (instruction_type) {
            // clang-format off
          case IT::Sb: assembler.strb(value_reg, address_reg, 0); break;
          case IT::Sh: assembler.strh(value_reg, address_reg, 0); break;
          case IT::Sw: assembler.str(cast_to_32bit(value_reg), address_reg, 0); break;
          case IT::Sd: assembler.str(value_reg, address_reg, 0); break;
            // clang-format on

          default:
            unreachable();
        }

        break;
      }

      case IT::Addi:
      case IT::Xori:
      case IT::Ori:
      case IT::Andi:
      case IT::Addiw: {
        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto imm = instruction.imm();
        auto dest = A64R::X12;

        bool succeeded = false;

        switch (instruction_type) {
          case IT::Addi:
          case IT::Addiw: {
            if (imm == 0) {
              dest = a;
              succeeded = true;
            } else {
              if (a == a64::Register::Xzr) {
                assembler.macro_mov(dest, imm);
                succeeded = true;
              } else {
                succeeded = assembler.try_add_i(dest, a, imm);
              }
            }

            if (succeeded && instruction_type == InstructionType::Addiw) {
              assembler.sxtw(dest, dest);
            }

            break;
          }

            // clang-format off
          case IT::Xori:  succeeded = assembler.try_eor(dest, a, imm);  break;
          case IT::Ori:   succeeded = assembler.try_orr(dest, a, imm);  break;
          case IT::Andi:  succeeded = assembler.try_and_(dest, a, imm); break;
            // clang-format on

          default:
            unreachable();
        }

        if (!succeeded) {
          const auto b = load_immediate_or_zero(A64R::X11, instruction.imm());

          switch (instruction_type) {
              // clang-format off
            case IT::Addi:  assembler.add(dest, a, b);  break;
            case IT::Xori:  assembler.eor(dest, a, b);  break;
            case IT::Ori:   assembler.orr(dest, a, b);  break;
            case IT::Andi:  assembler.and_(dest, a, b); break;
            case IT::Addiw: assembler.add(dest, a, b);  assembler.sxtw(dest, dest); break;
              // clang-format on

            default:
              unreachable();
          }
        }

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Slli:
      case IT::Srli:
      case IT::Srai:
      case IT::Slliw:
      case IT::Srliw:
      case IT::Sraiw: {
        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto dest = A64R::X11;

        const auto a32 = cast_to_32bit(a);
        const auto dest32 = cast_to_32bit(dest);

        const auto shamt = instruction.shamt();

        switch (instruction_type) {
            // clang-format off
          case IT::Slli:  assembler.lsl(dest, a, shamt); break;
          case IT::Srli:  assembler.lsr(dest, a, shamt); break;
          case IT::Srai:  assembler.asr(dest, a, shamt); break;
          case IT::Slliw: assembler.lsl(dest32, a32, shamt); assembler.sxtw(dest, dest); break;
          case IT::Srliw: assembler.lsr(dest32, a32, shamt); assembler.sxtw(dest, dest); break;
          case IT::Sraiw: assembler.asr(dest32, a32, shamt); assembler.sxtw(dest, dest); break;
            // clang-format on

          default:
            unreachable();
        }

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Slt:
      case IT::Sltu: {
        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto b = load_register_or_zero(A64R::X11, instruction.rs2());
        const auto dest = A64R::X12;

        assembler.cmp(a, b);
        assembler.cset(
          dest, instruction_type == IT::Sltu ? a64::Condition::UnsignedLess : a64::Condition::Less);

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Slti:
      case IT::Sltiu: {
        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto imm = instruction.imm();
        const auto dest = A64R::X12;

        if (!assembler.try_cmp(a, imm)) {
          const auto b = load_immediate_or_zero(A64R::X11, imm);
          assembler.cmp(a, b);
        }

        assembler.cset(dest, instruction_type == IT::Sltiu ? a64::Condition::UnsignedLess
                                                           : a64::Condition::Less);

        store_register(instruction.rd(), dest);

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
        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto b = load_register_or_zero(A64R::X11, instruction.rs2());
        const auto dest = A64R::X12;

        const auto a32 = cast_to_32bit(a);
        const auto b32 = cast_to_32bit(b);
        const auto dest32 = cast_to_32bit(dest);

        switch (instruction_type) {
            // clang-format off
          case IT::Add: assembler.add(dest, a, b); break;
          case IT::Sub: assembler.sub(dest, a, b); break;
          case IT::Xor: assembler.eor(dest, a, b); break;
          case IT::Or:  assembler.orr(dest, a, b); break;
          case IT::And: assembler.and_(dest, a, b); break;
          case IT::Sll: assembler.lsl(dest, a, b); break;
          case IT::Srl: assembler.lsr(dest, a, b); break;
          case IT::Sra: assembler.asr(dest, a, b); break;
          case IT::Addw: assembler.add(dest32, a32, b32); assembler.sxtw(dest, dest); break;
          case IT::Subw: assembler.sub(dest32, a32, b32); assembler.sxtw(dest, dest); break;
          case IT::Sllw: assembler.lsl(dest32, a32, b32); assembler.sxtw(dest, dest); break;
          case IT::Srlw: assembler.lsr(dest32, a32, b32); assembler.sxtw(dest, dest); break;
          case IT::Sraw: assembler.asr(dest32, a32, b32); assembler.sxtw(dest, dest); break;
            // clang-format on

          default:
            unreachable();
        }

        store_register(instruction.rd(), dest);

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
        const auto a = load_register_or_zero(A64R::X10, instruction.rs1());
        const auto b = load_register_or_zero(A64R::X11, instruction.rs2());
        const auto dest = A64R::X12;

        const auto a32 = cast_to_32bit(a);
        const auto b32 = cast_to_32bit(b);
        const auto dest32 = cast_to_32bit(dest);

        switch (instruction_type) {
            // clang-format off
          case IT::Mul:   assembler.mul(dest, a, b); break;
          case IT::Mulw:  assembler.mul(dest32, a32, b32);  assembler.sxtw(dest, dest); break;
          case IT::Div:   assembler.sdiv(dest, a, b); break;
          case IT::Divw:  assembler.sdiv(dest32, a32, b32); assembler.sxtw(dest, dest); break;
          case IT::Divu:  assembler.udiv(dest, a, b); break;
          case IT::Divuw: assembler.udiv(dest32, a32, b32); assembler.sxtw(dest, dest); break;
            // clang-format on

          case IT::Rem: {
            assembler.sdiv(dest, a, b);
            assembler.msub(dest, dest, b, a);
            break;
          }
          case IT::Remu: {
            assembler.udiv(dest, a, b);
            assembler.msub(dest, dest, b, a);
            break;
          }
          case IT::Remw: {
            assembler.sdiv(dest32, a32, b32);
            assembler.msub(dest32, dest32, b32, a32);
            assembler.sxtw(dest, dest);
            break;
          }
          case IT::Remuw: {
            assembler.udiv(dest32, a32, b32);
            assembler.msub(dest32, dest32, b32, a32);
            assembler.sxtw(dest, dest);
            break;
          }

          default:
            unreachable();
        }

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Mulh:
      case IT::Mulhu:
      case IT::Mulhsu: {
        generate_exit(JitExitReason::UnsupportedInstruction);
        return false;
      }

      case IT::Fence: {
        break;
      }

      case IT::Ecall: {
        generate_exit(JitExitReason::Ecall);
        return false;
      }
      case IT::Ebreak: {
        generate_exit(JitExitReason::Ebreak);
        return false;
      }
      case IT::Undefined: {
        generate_exit(JitExitReason::UndefinedInstruction);
        return false;
      }

      default:
        fatal_error("unknown instruction {}", instruction);
    }

    return true;
  }

  void generate_block() {
    // We cannot use load_immediate here.
    assembler.macro_mov(base_pc_reg, int64_t(base_pc));
    assembler.mov(return_address_reg, A64R::X30);

    while (true) {
      uint32_t instruction_encoded;
      if (!memory.read(current_pc, instruction_encoded)) {
        generate_exit(JitExitReason::InstructionFetchFault);
        break;
      }

      Instruction instruction{instruction_encoded};
      if (!generate_instruction(instruction)) {
        break;
      }

      current_pc += 4;

      if (single_step) {
        generate_exit(JitExitReason::SingleStep);
        break;
      }
    }
  }
};

void* JitExecutor::generate_code(const Memory& memory, uint64_t pc) {
  a64::Assembler assembler;

  CodeGenerator code_generator{
    .assembler = assembler,
    .memory = memory,
    .max_code_blocks = code_buffer->max_block_count(),

#ifdef PRINT_EXECUTION_LOG
    .single_step = true,
#else
    .single_step = false,
#endif

    .base_pc = pc,
    .current_pc = pc,
  };

  code_generator.generate_block();

  const auto instructions = code_generator.assembler.assembled_instructions();
  const auto instruction_bytes = cast_instructions_to_bytes(instructions);

  if (code_dump) {
    code_dump->write(pc, instruction_bytes);
  }

  if (true) {
    log_info("jitted {:x}: {} instructions...", pc, instructions.size());
  }

  return code_buffer->insert(pc, instruction_bytes);
}

void JitExecutor::generate_trampoline() {
  a64::Assembler as;

  as.mov(A64R::X10, A64R::X0);
  as.stp(A64R::X10, A64R::X30, A64R::Sp, -16, a64::Writeback::Pre);

  as.ldr(CodeGenerator::register_state_reg, A64R::X10,
         offsetof(JitTrampolineBlock, register_state));
  as.ldr(CodeGenerator::memory_base_reg, A64R::X10, offsetof(JitTrampolineBlock, memory_base));
  as.ldr(CodeGenerator::memory_size_reg, A64R::X10, offsetof(JitTrampolineBlock, memory_size));
  as.ldr(CodeGenerator::block_base_reg, A64R::X10, offsetof(JitTrampolineBlock, block_base));
  as.ldr(CodeGenerator::max_executable_pc_reg, A64R::X10,
         offsetof(JitTrampolineBlock, max_executable_pc));
  as.ldr(CodeGenerator::code_base_reg, A64R::X10, offsetof(JitTrampolineBlock, code_base));
  as.ldr(CodeGenerator::outline_handlers_reg, A64R::X10,
         offsetof(JitTrampolineBlock, outline_handlers));

  as.ldr(A64R::X10, A64R::X10, offsetof(JitTrampolineBlock, entrypoint));
  as.blr(A64R::X10);

  as.ldp(A64R::X10, A64R::X30, A64R::Sp, 16, a64::Writeback::Post);

  as.str(CodeGenerator::exit_reason_reg, A64R::X10, offsetof(JitTrampolineBlock, exit_reason));
  as.str(CodeGenerator::exit_pc_reg, A64R::X10, offsetof(JitTrampolineBlock, exit_pc));

  as.ret();

  trampoline_fn =
    code_buffer->insert_standalone(cast_instructions_to_bytes(as.assembled_instructions()));
}

JitExecutor::JitExecutor(std::shared_ptr<JitCodeBuffer> code_buffer)
    : code_buffer(std::move(code_buffer)) {
  generate_trampoline();

  if (true) {
    code_dump = std::make_unique<JitCodeDump>("jit_dump.bin");
  }

  generate_outline_handlers(*this->code_buffer, outline_handlers);
}

JitExecutor::~JitExecutor() = default;

void JitExecutor::run(Memory& memory, Cpu& cpu) {
  while (true) {
    const auto pc = cpu.pc();

    auto code = code_buffer->get(pc);
    if (!code) {
      code = generate_code(memory, pc);
      verify(code, "failed to jit code at pc {:x}", pc);
    }

#ifdef PRINT_EXECUTION_LOG
    const auto previous_register_state = cpu.register_state();
#endif

    JitTrampolineBlock trampoline_block{
      .register_state = uint64_t(cpu.register_state().raw_table()),
      .memory_base = uint64_t(memory.contents()),
      .memory_size = memory.size(),
      .block_base = uint64_t(code_buffer->block_translation_table()),
      .max_executable_pc = code_buffer->max_block_count() * 4,
      .code_base = uint64_t(code_buffer->code_buffer_base()),
      .outline_handlers = uint64_t(&outline_handlers),
      .entrypoint = uint64_t(code),
    };

    reinterpret_cast<void (*)(JitTrampolineBlock*)>(trampoline_fn)(&trampoline_block);

    cpu.set_reg(Register::Pc, trampoline_block.exit_pc);

#ifdef PRINT_EXECUTION_LOG
    ExecutionLog::print_execution_step(previous_register_state, cpu.register_state());
#endif

    const auto exit_reason = JitExitReason(trampoline_block.exit_reason);
    if (exit_reason != JitExitReason::BlockNotGenerated &&
        exit_reason != JitExitReason::SingleStep) {
      break;
    }
  }
}
