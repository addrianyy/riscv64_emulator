#include "CodeGenerator.hpp"
#include "Exit.hpp"
#include "Registers.hpp"

#include <vm/Instruction.hpp>
#include <vm/jit/Utilities.hpp>

#include <base/Error.hpp>

#include <limits>

using namespace vm;
using namespace vm::jit::x64;

using namespace asmlib;

using CodeBufferFlags = jit::CodeBuffer::Flags;

constexpr x64::OperandSize access_size_log2_to_operand_size[]{
  x64::OperandSize::Bits8,
  x64::OperandSize::Bits16,
  x64::OperandSize::Bits32,
  x64::OperandSize::Bits64,
};

template <typename... Args>
static bool instruction_any_of(InstructionType checked_type, Args... args) {
  return ((args == checked_type) || ...);
}

struct CodeGenerator {
  x64::Assembler& as;
  const Memory& memory;
  const jit::CodeBuffer& code_buffer;

  bool single_step{};

  std::vector<CodegenContext::Exit>& pending_exits;

  uint64_t current_pc{};

  static x64::Operand register_operand(Register reg) {
    verify(reg != Register::Zero, "cannot get operand for zero register");
    verify(reg != Register::Pc, "cannot get operand for PC register");
    return x64::Memory::base_disp(RegisterAllocation::register_state, int32_t(reg) * 8);
  }

  static x64::Operand register_operand_or_zero(Register reg) {
    return reg == Register::Zero ? x64::Operand{0} : register_operand(reg);
  }

  void generate_exit(ArchExitReason reason) { generate_exit(reason, current_pc); }
  void generate_exit(ArchExitReason reason, uint64_t pc) {
    as.mov(RegisterAllocation::exit_pc, int64_t(pc));
    as.mov(RegisterAllocation::exit_reason, int64_t(reason));
    as.ret();
  }
  void generate_exit(ArchExitReason reason, X64R pc) {
    as.mov(RegisterAllocation::exit_pc, pc);
    as.mov(RegisterAllocation::exit_reason, int64_t(reason));
    as.ret();
  }

  void add_pending_exit(x64::Label label, ArchExitReason reason, uint64_t pc) {
    pending_exits.push_back({
      .label = label,
      .reason = reason,
      .pc_value = pc,
    });
  }
  void add_pending_exit(x64::Label label, ArchExitReason reason, X64R pc) {
    pending_exits.push_back({
      .label = label,
      .reason = reason,
      .pc_register = pc,
    });
  }
  void generate_pending_exits() {
    for (const auto& pending_exit : pending_exits) {
      as.insert_label(pending_exit.label);

      if (pending_exit.pc_register != X64R::Rsp) {
        generate_exit(pending_exit.reason, pending_exit.pc_register);
      } else {
        generate_exit(pending_exit.reason, pending_exit.pc_value);
      }
    }
  }

  void load_register(X64R target, Register reg, int64_t offset = 0) {
    if (reg == Register::Zero) {
      as.mov(target, offset);
    } else {
      as.mov(target, register_operand(reg));
      if (offset != 0) {
        as.add(target, offset);
      }
    }
  }

  void store_register(Register target, X64R reg, bool sign_extend = false) {
    if (sign_extend) {
      as.movsxd(reg, reg);
    }
    as.mov(register_operand(target), reg);
  }

  void store_imm_to_register(Register reg, X64R scratch, int64_t imm) {
    if (reg == Register::Zero) {
      return;
    }

    using I32Limits = std::numeric_limits<int32_t>;

    if (imm >= I32Limits::min() && imm <= I32Limits::max()) {
      as.mov(register_operand(reg), imm);
    } else {
      as.mov(scratch, imm);
      as.mov(register_operand(reg), scratch);
    }
  }
  void store_uimm_to_register(Register reg, X64R scratch, uint64_t imm) {
    store_imm_to_register(reg, scratch, int64_t(imm));
  }

  void generate_validate_memory_access(X64R address,
                                       X64R scratch1,
                                       X64R scratch2,
                                       uint64_t access_size_log2,
                                       bool write) {
    const auto fault_label = as.allocate_label();

    // Make sure that address is aligned otherwise the bound check later won't be accurate.
    if (access_size_log2 > 0) {
      as.test(address, (1 << access_size_log2) - 1);
      as.jnz(fault_label);
    }

    // Check if address >= memory_size. We don't need to account for the access size because we
    // have already checked for alignment.
    as.cmp(address, int64_t(memory.size()));
    as.jae(fault_label);

    if ((code_buffer.flags() & CodeBufferFlags::SkipPermissionChecks) == CodeBufferFlags::None) {
      const auto operand_size = access_size_log2_to_operand_size[access_size_log2];

      const auto perms_reg = scratch1;
      const auto mask_reg = scratch2;

      as.with_operand_size(operand_size, [&] {
        as.mov(perms_reg,
               x64::Memory::base_index(RegisterAllocation::permissions_base, address, 1));
      });

      {
        uint64_t mask = 0;
        for (size_t i = 0; i < (1 << access_size_log2); ++i) {
          mask |= uint64_t(write ? MemoryFlags::Write : MemoryFlags::Read) << (i * 8);
        }
        as.mov(mask_reg, int64_t(mask));
      }

      as.and_(perms_reg, mask_reg);
      as.cmp(perms_reg, mask_reg);
      as.jne(fault_label);
    }

    add_pending_exit(fault_label,
                     write ? ArchExitReason::MemoryWriteFault : ArchExitReason::MemoryReadFault,
                     current_pc);
  }

  x64::Label generate_validated_branch(X64R block_index) {
    const auto no_block_label = as.allocate_label();

    as.with_operand_size(x64::OperandSize::Bits32, [&] {
      as.mov(block_index, x64::Memory::base_index(RegisterAllocation::block_base, block_index, 4));
    });
    const auto code_offset = block_index;

    as.test(code_offset, code_offset);
    as.jz(no_block_label);

    as.add(code_offset, RegisterAllocation::code_base);
    as.jmp(code_offset);

    return no_block_label;
  }

  void generate_static_branch(uint64_t target_pc, X64R scratch) {
    const auto block = target_pc / 4;

    // We can statically handle some error conditions.
    if ((target_pc & 3) != 0) {
      return generate_exit(ArchExitReason::UnalignedPc);
    }
    if (block >= code_buffer.max_block_count()) {
      return generate_exit(ArchExitReason::OutOfBoundsPc);
    }

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(ArchExitReason::SingleStep, target_pc);
    } else {
      as.mov(scratch, int64_t(block));

      const auto exit_label = generate_validated_branch(scratch);
      add_pending_exit(exit_label, ArchExitReason::BlockNotGenerated, target_pc);
    }
  }

  void generate_dynamic_branch(X64R target_pc, X64R scratch) {
    const auto unaligned_label = as.allocate_label();
    const auto oob_label = as.allocate_label();

    // Exit the VM if the address is not properly aligned.
    as.test(target_pc, 0b10);
    as.jnz(unaligned_label);

    // Calculate block index from PC.
    as.mov(scratch, target_pc);
    as.shr(scratch, 2);

    // Exit the VM if target_pc >= max_executable_pc.
    as.cmp(scratch, int64_t(code_buffer.max_block_count()));
    as.jae(oob_label);

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(ArchExitReason::SingleStep, target_pc);
    } else {
      const auto exit_label = generate_validated_branch(scratch);
      add_pending_exit(exit_label, ArchExitReason::BlockNotGenerated, target_pc);
    }

    add_pending_exit(unaligned_label, ArchExitReason::UnalignedPc, target_pc);
    add_pending_exit(oob_label, ArchExitReason::OutOfBoundsPc, target_pc);
  }

  void generate_binary_operation(InstructionType instruction_type,
                                 x64::Operand op1,
                                 x64::Operand op2) {
    using IT = InstructionType;

    switch (instruction_type) {
      case IT::Add:
      case IT::Addi:
      case IT::Addw:
      case IT::Addiw:
        as.add(op1, op2);
        break;

      case IT::Sub:
      case IT::Subw:
        as.sub(op1, op2);
        break;

      case IT::Xor:
      case IT::Xori:
        as.xor_(op1, op2);
        break;

      case IT::Or:
      case IT::Ori:
        as.or_(op1, op2);
        break;

      case IT::And:
      case IT::Andi:
        as.and_(op1, op2);
        break;

      case IT::Sll:
      case IT::Slli:
      case IT::Sllw:
      case IT::Slliw:
        as.shl(op1, op2);
        break;

      case IT::Srl:
      case IT::Srlw:
      case IT::Srli:
      case IT::Srliw:
        as.shr(op1, op2);
        break;

      case IT::Sra:
      case IT::Srai:
      case IT::Sraiw:
      case IT::Sraw:
        as.sar(op1, op2);
        break;

      default:
        unreachable();
    }
  }

  bool generate_instruction(const Instruction& instruction) {
    const auto instruction_type = instruction.type();

    using IT = InstructionType;

    switch (instruction_type) {
      case IT::Lui: {
        if (instruction.rd() != Register::Zero) {
          store_imm_to_register(instruction.rd(), RegisterAllocation::a_reg, instruction.imm());
        }
        break;
      }

      case IT::Auipc: {
        if (instruction.rd() != Register::Zero) {
          store_uimm_to_register(instruction.rd(), RegisterAllocation::a_reg,
                                 current_pc + instruction.imm());
        }
        break;
      }

      case IT::Jal: {
        if (instruction.rd() != Register::Zero) {
          store_uimm_to_register(instruction.rd(), RegisterAllocation::a_reg, current_pc + 4);
        }

        const auto target = current_pc + instruction.imm();
        generate_static_branch(target, RegisterAllocation::a_reg);

        return false;
      }

      case IT::Jalr: {
        load_register(RegisterAllocation::a_reg, instruction.rs1(), instruction.imm());

        if (instruction.rd() != Register::Zero) {
          store_uimm_to_register(instruction.rd(), RegisterAllocation::b_reg, current_pc + 4);
        }

        generate_dynamic_branch(RegisterAllocation::a_reg, RegisterAllocation::b_reg);

        return false;
      }

      case IT::Beq:
      case IT::Bne:
      case IT::Blt:
      case IT::Bge:
      case IT::Bltu:
      case IT::Bgeu: {
        load_register(RegisterAllocation::a_reg, instruction.rs1());

        as.cmp(RegisterAllocation::a_reg, register_operand_or_zero(instruction.rs2()));

        const auto fallthrough = as.allocate_label();

        switch (instruction_type) {
            // clang-format off
          case IT::Beq: as.jne(fallthrough); break;
          case IT::Bne: as.je(fallthrough); break;
          case IT::Blt: as.jnl(fallthrough); break;
          case IT::Bge: as.jnge(fallthrough); break;
          case IT::Bltu: as.jnb(fallthrough); break;
          case IT::Bgeu: as.jnae(fallthrough); break;
            // clang-format on

          default:
            unreachable();
        }

        generate_static_branch(current_pc + instruction.imm(), RegisterAllocation::a_reg);

        as.insert_label(fallthrough);

        break;
      }

      case IT::Lb:
      case IT::Lh:
      case IT::Lw:
      case IT::Ld:
      case IT::Lbu:
      case IT::Lhu:
      case IT::Lwu: {
        if (instruction.rd() != Register::Zero) {
          load_register(RegisterAllocation::a_reg, instruction.rs1(), instruction.imm());
          generate_validate_memory_access(
            RegisterAllocation::a_reg, RegisterAllocation::b_reg, RegisterAllocation::c_reg,
            jit::utils::memory_access_size_log2(instruction_type), false);

          const auto address =
            x64::Memory::base_index(RegisterAllocation::memory_base, RegisterAllocation::a_reg, 1);
          const auto dest = RegisterAllocation::b_reg;

          switch (instruction_type) {
              // clang-format off
            case IT::Lb: as.movsxb(dest, address); break;
            case IT::Lbu: as.movzxb(dest, address); break;
            case IT::Lh: as.movsxw(dest, address); break;
            case IT::Lhu: as.movzxw(dest, address); break;
            case IT::Lw: as.movsxd(dest, address); break;
            case IT::Ld: as.mov(dest, address); break;
              // clang-format on

            case IT::Lwu: {
              as.with_operand_size(x64::OperandSize::Bits32, [&] { as.mov(dest, address); });
              break;
            }

            default:
              unreachable();
          }

          store_register(instruction.rd(), dest);
        }

        break;
      }

      case IT::Sb:
      case IT::Sh:
      case IT::Sw:
      case IT::Sd: {
        const auto access_size_log2 = jit::utils::memory_access_size_log2(instruction_type);

        load_register(RegisterAllocation::a_reg, instruction.rs1(), instruction.imm());
        generate_validate_memory_access(RegisterAllocation::a_reg, RegisterAllocation::b_reg,
                                        RegisterAllocation::c_reg, access_size_log2, true);

        load_register(RegisterAllocation::b_reg, instruction.rs2());

        const auto operand_size = access_size_log2_to_operand_size[access_size_log2];
        const auto address =
          x64::Memory::base_index(RegisterAllocation::memory_base, RegisterAllocation::a_reg, 1);

        as.with_operand_size(operand_size, [&] { as.mov(address, RegisterAllocation::b_reg); });

        break;
      }

      case IT::Addi:
      case IT::Xori:
      case IT::Ori:
      case IT::Andi:
      case IT::Addiw:
      case IT::Slli:
      case IT::Srli:
      case IT::Srai:
      case IT::Slliw:
      case IT::Srliw:
      case IT::Sraiw: {
        // Instructions with 1st source == destination and 64 bit operand size:
        // op [rd], imm

        // Other instructions:
        // mov rax, [rs1]
        // op  rax, imm
        // (movsx rax, eax)
        // mov [rd], rax

        if (instruction.rd() != Register::Zero) {
          if (instruction_type == IT::Addi && instruction.rs1() == Register::Zero) {
            // li pseudoinstruction
            store_imm_to_register(instruction.rd(), RegisterAllocation::a_reg, instruction.imm());
          } else if (instruction_type == IT::Addi && instruction.imm() == 0) {
            // mv pseudoinstruction
            load_register(RegisterAllocation::a_reg, instruction.rs1());
            store_register(instruction.rd(), RegisterAllocation::a_reg);
          } else if (instruction_type == IT::Addiw && instruction.imm() == 0) {
            // sext.w pseudoinstruction
            as.movsxd(RegisterAllocation::a_reg, register_operand_or_zero(instruction.rs1()));
            store_register(instruction.rd(), RegisterAllocation::a_reg);
          } else {
            const auto is_32bit =
              instruction_any_of(instruction_type, IT::Addiw, IT::Slliw, IT::Srliw, IT::Sraiw);

            if (!is_32bit && instruction.rd() == instruction.rs1()) {
              generate_binary_operation(instruction_type, register_operand(instruction.rd()),
                                        instruction.imm());
            } else {
              load_register(RegisterAllocation::a_reg, instruction.rs1());
              generate_binary_operation(instruction_type, RegisterAllocation::a_reg,
                                        instruction.imm());
              store_register(instruction.rd(), RegisterAllocation::a_reg, is_32bit);
            }
          }
        }

        break;
      }

      case IT::Slt:
      case IT::Sltu:
      case IT::Slti:
      case IT::Sltiu: {
        if (instruction.rd() != Register::Zero) {
          const auto has_imm = instruction_any_of(instruction_type, IT::Slti, IT::Sltiu);
          const auto is_unsigned = instruction_any_of(instruction_type, IT::Sltu, IT::Sltiu);

          // Instructions with immediate second argument:
          // xor   rax, rax
          // cmp   [rs1], imm
          // setcc al
          // mov   [rd], rax

          // Instructions with register second argument:
          // xor   rax, rax
          // mov   rcx, [rs1]
          // cmp   rcx, [rs2]
          // setcc al
          // mov   [rd], rax

          as.xor_(RegisterAllocation::a_reg, RegisterAllocation::a_reg);
          if (has_imm) {
            as.cmp(register_operand(instruction.rs1()), instruction.imm());
          } else {
            load_register(RegisterAllocation::b_reg, instruction.rs1());
            as.cmp(RegisterAllocation::b_reg, register_operand_or_zero(instruction.rs2()));
          }

          if (is_unsigned) {
            as.setb(RegisterAllocation::a_reg);
          } else {
            as.setl(RegisterAllocation::a_reg);
          }

          store_register(instruction.rd(), RegisterAllocation::a_reg);
        }

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
        // Instructions with 1st source == destination and 64 bit operand size:
        // mov rcx, [rs2]
        // op  [rd], rcx

        // Other shift instructions:
        // mov rax, [rs1]
        // mov rcx, [rs2]
        // op  rax, rcx
        // (movsx rax, eax)
        // mov [rd], rax

        // Other instructions:
        // mov rax, [rs1]
        // op  rax, [rs2]
        // (movsx rax, eax)
        // mov [rd], rax

        if (instruction.rd() != Register::Zero) {
          const auto is_32bit =
            instruction_any_of(instruction_type, IT::Addw, IT::Subw, IT::Sllw, IT::Srlw, IT::Sraw);
          const auto is_shift = instruction_any_of(instruction_type, IT::Sll, IT::Srl, IT::Sra,
                                                   IT::Sllw, IT::Srlw, IT::Sraw);

          if (!is_32bit && instruction.rd() == instruction.rs1()) {
            generate_binary_operation(instruction_type, register_operand(instruction.rd()),
                                      instruction.imm());
          } else {
            load_register(RegisterAllocation::a_reg, instruction.rs1());

            if (is_shift) {
              load_register(RegisterAllocation::c_reg, instruction.rs2());
            }

            generate_binary_operation(
              instruction_type, RegisterAllocation::a_reg,
              is_shift ? RegisterAllocation::c_reg : register_operand_or_zero(instruction.rs2()));

            store_register(instruction.rd(), RegisterAllocation::a_reg, is_32bit);
          }
        }

        break;
      }

      case IT::Mul:
      case IT::Mulw: {
        if (instruction.rd() != Register::Zero) {
          const auto is_32bit = instruction_any_of(instruction_type, IT::Mulw);

          load_register(RegisterAllocation::a_reg, instruction.rs1());
          load_register(RegisterAllocation::b_reg, instruction.rs2());

          as.with_operand_size(is_32bit ? x64::OperandSize::Bits32 : x64::OperandSize::Bits64, [&] {
            as.imul(RegisterAllocation::a_reg, RegisterAllocation::b_reg);
          });

          store_register(instruction.rd(), RegisterAllocation::a_reg, is_32bit);
        }

        break;
      }

      case IT::Div:
      case IT::Divw:
      case IT::Divu:
      case IT::Divuw:
      case IT::Rem:
      case IT::Remu:
      case IT::Remw:
      case IT::Remuw: {
        if (instruction.rd() != Register::Zero) {
          const auto is_32bit =
            instruction_any_of(instruction_type, IT::Divw, IT::Divuw, IT::Remw, IT::Remuw);
          const auto is_unsigned =
            instruction_any_of(instruction_type, IT::Divu, IT::Divuw, IT::Remu, IT::Remuw);
          const auto is_remainder =
            instruction_any_of(instruction_type, IT::Rem, IT::Remu, IT::Remw, IT::Remuw);

          const auto operand_size = is_32bit ? x64::OperandSize::Bits32 : x64::OperandSize::Bits64;

          load_register(X64R::Rax, instruction.rs1());
          load_register(X64R::Rbx, instruction.rs2());

          const auto continue_division = as.allocate_label();
          const auto skip_division = as.allocate_label();

          // Handle case where signed disivion may overflow and crash the program.
          if (!is_unsigned) {
            as.cmp(X64R::Rbx, -1);
            as.jne(continue_division);

            if (is_32bit) {
              as.cmp(X64R::Rax, std::numeric_limits<int32_t>::min());
            } else {
              as.mov(X64R::Rdx, std::numeric_limits<int64_t>::min());
              as.cmp(X64R::Rax, X64R::Rdx);
            }

            as.jne(continue_division);

            store_uimm_to_register(instruction.rd(), RegisterAllocation::a_reg, 0);

            as.jmp(skip_division);
          }

          as.insert_label(continue_division);

          as.with_operand_size(operand_size, [&] {
            if (is_unsigned) {
              as.xor_(X64R::Rdx, X64R::Rdx);
              as.div(X64R::Rbx);
            } else {
              as.cqo();
              as.idiv(X64R::Rbx);
            }
          });

          store_register(instruction.rd(), is_remainder ? X64R::Rdx : X64R::Rax, is_32bit);

          as.insert_label(skip_division);
        }

        break;
      }

      case IT::Mulh:
      case IT::Mulhu:
      case IT::Mulhsu: {
        generate_exit(ArchExitReason::UnsupportedInstruction);
        return false;
      }

      case IT::Fence: {
        break;
      }

      case IT::Ecall: {
        generate_exit(ArchExitReason::Ecall);
        return false;
      }
      case IT::Ebreak: {
        generate_exit(ArchExitReason::Ebreak);
        return false;
      }
      case IT::Undefined: {
        generate_exit(ArchExitReason::UndefinedInstruction);
        return false;
      }

      default:
        fatal_error("unknown instruction {}", instruction);
    }

    return true;
  }

  void generate_block(uint64_t block_pc) {
    current_pc = block_pc;

    while (true) {
      uint32_t instruction_encoded;
      if (!memory.read(current_pc, MemoryFlags::Execute, instruction_encoded)) {
        generate_exit(ArchExitReason::InstructionFetchFault);
        break;
      }

      Instruction instruction{instruction_encoded};
      if (!generate_instruction(instruction)) {
        break;
      }

      current_pc += 4;

      if (single_step) {
        generate_exit(ArchExitReason::SingleStep);
        break;
      }
    }
  }

  void generate_code(uint64_t pc) {
    current_pc = pc;

    generate_block(pc);
    generate_pending_exits();
  }
};

std::span<const uint8_t> jit::x64::generate_block_code(CodegenContext& context,
                                                       const CodeBuffer& code_buffer,
                                                       const Memory& memory,
                                                       bool single_step,
                                                       uint64_t pc) {
  context.prepare();

  CodeGenerator code_generator{
    .as = context.assembler,
    .memory = memory,
    .code_buffer = code_buffer,
    .single_step = single_step,
    .pending_exits = context.pending_exits,
  };

  code_generator.generate_code(pc);

  return code_generator.as.assembled_instructions();
}
