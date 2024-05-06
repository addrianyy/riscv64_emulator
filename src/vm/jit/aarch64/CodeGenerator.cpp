#include "CodeGenerator.hpp"
#include "Utilities.hpp"

#include <vm/Instruction.hpp>

#include "base/Log.hpp"

using namespace vm;
using namespace vm::jit::aarch64;

using CodeBufferFlags = jit::CodeBuffer::Flags;

struct CodeGenerator {
  a64::Assembler& as;
  const Memory& memory;
  const jit::CodeBuffer& code_buffer;

  bool single_step{};

  std::vector<CodegenContext::Exit>& pending_exits;

  uint64_t base_pc{};
  uint64_t current_pc{};

  RegisterCache register_cache{as};

  void load_immediate(A64R target, int64_t immediate) {
    const auto pc_offset = int64_t(immediate) - int64_t(base_pc);
    const auto max_pc_delta = int64_t(std::numeric_limits<int16_t>::max());

    // Roughly estimate if it's worth trying to use pc-based immediates.
    if (pc_offset >= -max_pc_delta && pc_offset <= max_pc_delta) {
      if (as.try_add_i(target, RegisterAllocation::base_pc, pc_offset)) {
        return;
      }
    }

    as.macro_mov(target, immediate);
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

  A64R add_offset_to_register(A64R value_reg, A64R scratch_reg, int64_t offset) {
    if (value_reg == A64R::Xzr) {
      load_immediate(scratch_reg, offset);
      return scratch_reg;
    }

    if (offset == 0) {
      return value_reg;
    }

    if (!as.try_add_i(scratch_reg, value_reg, offset)) {
      load_immediate(scratch_reg, offset);
      as.add(scratch_reg, value_reg, scratch_reg);
    }

    return scratch_reg;
  }

  void generate_exit(ArchExitReason reason) { generate_exit(reason, current_pc); }
  void generate_exit(ArchExitReason reason, uint64_t pc) {
    register_cache.flush_current_registers();
    load_immediate_u(RegisterAllocation::exit_reason, uint64_t(reason));
    load_immediate_u(RegisterAllocation::exit_pc, pc);
    as.ret();
  }
  void generate_exit(ArchExitReason reason, A64R pc) {
    register_cache.flush_current_registers();
    load_immediate_u(RegisterAllocation::exit_reason, uint64_t(reason));
    as.mov(RegisterAllocation::exit_pc, pc);
    as.ret();
  }

  void add_pending_exit(a64::Label label,
                        ArchExitReason reason,
                        bool flush_registers,
                        uint64_t pc) {
    pending_exits.push_back({
      .label = label,
      .reason = reason,
      .pc_value = pc,
      .snapshot =
        flush_registers ? register_cache.take_state_snapshot() : RegisterCache::StateSnapshot{},
    });
  }
  void add_pending_exit(a64::Label label, ArchExitReason reason, bool flush_registers, A64R pc) {
    pending_exits.push_back({
      .label = label,
      .reason = reason,
      .pc_register = pc,
      .snapshot =
        flush_registers ? register_cache.take_state_snapshot() : RegisterCache::StateSnapshot{},
    });
  }
  void generate_pending_exits() {
    for (const auto& pending_exit : pending_exits) {
      as.insert_label(pending_exit.label);

      register_cache.flush_registers(pending_exit.snapshot);

      load_immediate_u(RegisterAllocation::exit_reason, uint64_t(pending_exit.reason));

      if (pending_exit.pc_register != A64R::Xzr) {
        as.mov(RegisterAllocation::exit_pc, pending_exit.pc_register);
      } else {
        load_immediate_u(RegisterAllocation::exit_pc, pending_exit.pc_value);
      }

      as.ret();
    }
  }

  bool load_memory_permission_mask(A64R target, MemoryFlags flags, size_t access_size_log2) {
    const auto uflags = uint64_t(flags);

    switch (access_size_log2) {
      case 0: {
        as.mov(target, uflags);
        break;
      }

      case 1: {
        as.mov(target, (uflags << 8) | uflags);
        break;
      }

      case 2:
      case 3: {
        uint64_t mask = 0;
        for (size_t i = 0; i < 8; ++i) {
          mask |= uflags << (i * 8);
        }
        as.mov(target, mask);

        // Truncate to 32 bit registers when access size == 4.
        return access_size_log2 == 2;
      }

      default:
        unreachable();
    }

    return false;
  }

  void generate_validate_memory_access(A64R address_reg,
                                       A64R scratch_reg,
                                       A64R scratch_reg2,
                                       uint64_t access_size_log2,
                                       bool write) {
    const auto fault_label = as.allocate_label();

    // Make sure that address is aligned otherwise the bound check later won't be accurate.
    if (access_size_log2 > 0) {
      as.tst(address_reg, (1 << access_size_log2) - 1);
      as.b(a64::Condition::NotZero, fault_label);
    }

    // Check if address >= memory_size. We don't need to account for the access size because we
    // have already checked for alignment.
    as.cmp(address_reg, RegisterAllocation::memory_size);
    as.b(a64::Condition::UnsignedGreaterEqual, fault_label);

    if ((code_buffer.flags() & CodeBufferFlags::SkipPermissionChecks) == CodeBufferFlags::None) {
      auto perms_reg = scratch_reg;
      auto mask_reg = scratch_reg2;

      const auto pb = RegisterAllocation::permissions_base;

      switch (access_size_log2) {
          // clang-format off
        case 0: as.ldrb(perms_reg, pb, address_reg); break;
        case 1: as.ldrh(perms_reg, pb, address_reg); break;
        case 2: as.ldr(cast_to_32bit(perms_reg), pb, address_reg); break;
        case 3: as.ldr(perms_reg, pb, address_reg); break;
          // clang-format on

        default:
          unreachable();
      }

      const auto truncate_to_32bit = load_memory_permission_mask(
        mask_reg, write ? MemoryFlags::Write : MemoryFlags::Read, access_size_log2);

      if (truncate_to_32bit) {
        perms_reg = cast_to_32bit(perms_reg);
        mask_reg = cast_to_32bit(mask_reg);
      }

      if (current_pc == 0x23704) {
        as.brk(1);
      }

      as.and_(perms_reg, perms_reg, mask_reg);
      as.cmp(perms_reg, mask_reg);
      as.b(a64::Condition::NotEqual, fault_label);
    }

    add_pending_exit(fault_label,
                     write ? ArchExitReason::MemoryWriteFault : ArchExitReason::MemoryReadFault,
                     true, current_pc);
  }

  a64::Label generate_validated_branch(A64R block_offset_reg) {
    // Load the 32 bit code offset from block translation table.
    if ((code_buffer.flags() & CodeBufferFlags::Multithreaded) == CodeBufferFlags::None) {
      as.ldr(cast_to_32bit(block_offset_reg), RegisterAllocation::block_base, block_offset_reg);
    } else {
      as.add(block_offset_reg, RegisterAllocation::block_base, block_offset_reg);
      as.ldar(cast_to_32bit(block_offset_reg), block_offset_reg);
    }

    const auto code_offset_reg = block_offset_reg;

    // Exit the VM if the block isn't generated yet.
    const auto no_block_label = as.allocate_label();
    as.cbz(code_offset_reg, no_block_label);

    // Jump to the block.
    as.add(code_offset_reg, RegisterAllocation::code_base, code_offset_reg);
    as.br(code_offset_reg);

    return no_block_label;
  }

  void generate_static_branch(uint64_t target_pc, A64R scratch_reg) {
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
      // Calculate the memory offset from `block_base`.
      load_immediate_u(scratch_reg, block * 4);

      register_cache.flush_current_registers();

      const auto exit_label = generate_validated_branch(scratch_reg);
      add_pending_exit(exit_label, ArchExitReason::BlockNotGenerated, false, target_pc);
    }
  }

  void generate_dynamic_branch(A64R target_pc, A64R scratch_reg) {
    verify(target_pc != scratch_reg, "target_pc cannot be equal to scratch_reg");

    const auto oob_label = as.allocate_label();
    const auto unaligned_label = as.allocate_label();

    // Mask off last bit as it is required by the architecture.
    as.and_(scratch_reg, target_pc, ~uint64_t(1));

    register_cache.flush_current_registers();

    // Exit the VM if the address is not properly aligned.
    as.tst(scratch_reg, 0b11);
    as.b(a64::Condition::NotZero, unaligned_label);

    // Exit the VM if target_pc >= max_executable_pc.
    as.cmp(scratch_reg, RegisterAllocation::max_executable_pc);
    as.b(a64::Condition::UnsignedGreaterEqual, oob_label);

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(ArchExitReason::SingleStep, target_pc);
    } else {
      const auto exit_label = generate_validated_branch(scratch_reg);
      add_pending_exit(exit_label, ArchExitReason::BlockNotGenerated, false, target_pc);
    }

    add_pending_exit(oob_label, ArchExitReason::OutOfBoundsPc, false, target_pc);
    add_pending_exit(unaligned_label, ArchExitReason::UnalignedPc, false, target_pc);
  }

  bool generate_instruction(const Instruction& instruction) {
    const auto instruction_type = instruction.type();

    using IT = InstructionType;

    switch (instruction_type) {
      case IT::Lui: {
        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          const auto reg = register_cache.lock_register(rd);
          load_immediate(reg, instruction.imm());
          register_cache.unlock_register_dirty(reg);
        }
        break;
      }

      case IT::Auipc: {
        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          const auto reg = register_cache.lock_register(rd);
          load_immediate_u(reg, current_pc + instruction.imm());
          register_cache.unlock_register_dirty(reg);
        }
        break;
      }

      case InstructionType::Jal: {
        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          const auto reg = register_cache.lock_register(rd);
          load_immediate_u(reg, current_pc + 4);
          register_cache.unlock_register_dirty(reg);
        }

        const auto target = current_pc + instruction.imm();
        generate_static_branch(target, RegisterAllocation::a_reg);

        return false;
      }

      case InstructionType::Jalr: {
        const auto target_reg = register_cache.lock_register(instruction.rs1());
        auto offseted_reg =
          add_offset_to_register(target_reg, RegisterAllocation::a_reg, instruction.imm());

        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          const auto dest_reg = register_cache.lock_register(rd);

          if (dest_reg == offseted_reg) {
            as.mov(RegisterAllocation::a_reg, offseted_reg);
            offseted_reg = RegisterAllocation::a_reg;
          }

          load_immediate_u(dest_reg, current_pc + 4);

          register_cache.unlock_register_dirty(dest_reg);
        }

        generate_dynamic_branch(offseted_reg, RegisterAllocation::b_reg);

        register_cache.unlock_register(target_reg);

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

        const auto [a, b] = register_cache.lock_registers(instruction.rs1(), instruction.rs2());

        const auto skip_label = as.allocate_label();

        as.cmp(a, b);
        as.b(condition, skip_label);

        generate_static_branch(current_pc + instruction.imm(), RegisterAllocation::a_reg);

        as.insert_label(skip_label);

        register_cache.unlock_registers(a, b);

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
          const auto [unoffseted_address_reg, dest_reg] =
            register_cache.lock_registers(instruction.rs1(), instruction.rd());

          const auto address_reg = add_offset_to_register(
            unoffseted_address_reg, RegisterAllocation::a_reg, instruction.imm());

          generate_validate_memory_access(address_reg, RegisterAllocation::b_reg,
                                          RegisterAllocation::c_reg,
                                          utils::memory_access_size_log2(instruction_type), false);

          const auto mb = RegisterAllocation::memory_base;

          switch (instruction_type) {
              // clang-format off
            case IT::Lb:  as.ldrsb(dest_reg, mb, address_reg); break;
            case IT::Lh:  as.ldrsh(dest_reg, mb, address_reg); break;
            case IT::Lw:  as.ldrsw(dest_reg, mb, address_reg); break;
            case IT::Ld:  as.ldr(dest_reg, mb, address_reg); break;
            case IT::Lbu: as.ldrb(dest_reg, mb, address_reg); break;
            case IT::Lhu: as.ldrh(dest_reg, mb, address_reg); break;
            case IT::Lwu: as.ldr(cast_to_32bit(dest_reg), mb, address_reg); break;
              // clang-format on

            default:
              unreachable();
          }

          register_cache.unlock_register(unoffseted_address_reg);
          register_cache.unlock_register_dirty(dest_reg);
        }

        break;
      }

      case IT::Sb:
      case IT::Sh:
      case IT::Sw:
      case IT::Sd: {
        const auto [unoffseted_address_reg, value_reg] =
          register_cache.lock_registers(instruction.rs1(), instruction.rs2());

        const auto address_reg = add_offset_to_register(
          unoffseted_address_reg, RegisterAllocation::a_reg, instruction.imm());

        generate_validate_memory_access(address_reg, RegisterAllocation::b_reg,
                                        RegisterAllocation::c_reg,
                                        utils::memory_access_size_log2(instruction_type), true);

        const auto mb = RegisterAllocation::memory_base;

        switch (instruction_type) {
            // clang-format off
          case IT::Sb: as.strb(value_reg, mb, address_reg); break;
          case IT::Sh: as.strh(value_reg, mb, address_reg); break;
          case IT::Sw: as.str(cast_to_32bit(value_reg), mb, address_reg); break;
          case IT::Sd: as.str(value_reg, mb, address_reg); break;
            // clang-format on

          default:
            unreachable();
        }

        register_cache.unlock_registers(unoffseted_address_reg, value_reg);

        break;
      }

      case IT::Addi:
      case IT::Xori:
      case IT::Ori:
      case IT::Andi:
      case IT::Addiw: {
        if (instruction.rd() != Register::Zero) {
          const auto [a, dest] = register_cache.lock_registers(instruction.rs1(), instruction.rd());
          const auto imm = instruction.imm();

          bool succeeded = false;

          switch (instruction_type) {
            case IT::Addi:
            case IT::Addiw: {
              if (imm == 0) {
                as.mov(dest, a);
                succeeded = true;
              } else {
                // Add takes SP as second operand so we need to to special case zero register.
                if (a == a64::Register::Xzr) {
                  load_immediate(dest, imm);
                  succeeded = true;
                } else {
                  succeeded = as.try_add_i(dest, a, imm);
                }
              }

              if (succeeded && instruction_type == InstructionType::Addiw) {
                as.sxtw(dest, dest);
              }

              break;
            }

              // clang-format off
            case IT::Xori:  succeeded = as.try_eor(dest, a, imm);  break;
            case IT::Ori:   succeeded = as.try_orr(dest, a, imm);  break;
            case IT::Andi:  succeeded = as.try_and_(dest, a, imm); break;
              // clang-format on

            default:
              unreachable();
          }

          if (!succeeded) {
            const auto b = load_immediate_or_zero(RegisterAllocation::a_reg, imm);

            switch (instruction_type) {
                // clang-format off
              case IT::Addi:  as.add(dest, a, b);  break;
              case IT::Xori:  as.eor(dest, a, b);  break;
              case IT::Ori:   as.orr(dest, a, b);  break;
              case IT::Andi:  as.and_(dest, a, b); break;
              case IT::Addiw: as.add(dest, a, b);  as.sxtw(dest, dest); break;
                // clang-format on

              default:
                unreachable();
            }
          }

          register_cache.unlock_register(a);
          register_cache.unlock_register_dirty(dest);
        }

        break;
      }

      case IT::Slli:
      case IT::Srli:
      case IT::Srai:
      case IT::Slliw:
      case IT::Srliw:
      case IT::Sraiw: {
        if (instruction.rd() != Register::Zero) {
          const auto [a, dest] = register_cache.lock_registers(instruction.rs1(), instruction.rd());

          const auto a32 = cast_to_32bit(a);
          const auto dest32 = cast_to_32bit(dest);

          const auto shamt = instruction.shamt();

          switch (instruction_type) {
              // clang-format off
            case IT::Slli:  as.lsl(dest, a, shamt); break;
            case IT::Srli:  as.lsr(dest, a, shamt); break;
            case IT::Srai:  as.asr(dest, a, shamt); break;
            case IT::Slliw: as.lsl(dest32, a32, shamt); as.sxtw(dest, dest); break;
            case IT::Srliw: as.lsr(dest32, a32, shamt); as.sxtw(dest, dest); break;
            case IT::Sraiw: as.asr(dest32, a32, shamt); as.sxtw(dest, dest); break;
              // clang-format on

            default:
              unreachable();
          }

          register_cache.unlock_register(a);
          register_cache.unlock_register_dirty(dest);
        }

        break;
      }

      case IT::Slt:
      case IT::Sltu: {
        if (instruction.rd() != Register::Zero) {
          const auto [a, b, dest] =
            register_cache.lock_registers(instruction.rs1(), instruction.rs2(), instruction.rd());

          as.cmp(a, b);
          as.cset(dest, instruction_type == IT::Sltu ? a64::Condition::UnsignedLess
                                                     : a64::Condition::Less);

          register_cache.unlock_registers(a, b);
          register_cache.unlock_register_dirty(dest);
        }

        break;
      }

      case IT::Slti:
      case IT::Sltiu: {
        if (instruction.rd() != Register::Zero) {
          // cmp (immediate) takes SP as first operand so we need to special case zero register.
          if (instruction.rs1() == Register::Zero) {
            const auto dest = register_cache.lock_register(instruction.rd());

            load_immediate_u(dest, instruction_type == InstructionType::Slti
                                     ? (int64_t(0) < int64_t(instruction.imm()))
                                     : (uint64_t(0) < uint64_t(instruction.imm())));

            register_cache.unlock_register_dirty(dest);
          } else {
            const auto [a, dest] =
              register_cache.lock_registers(instruction.rs1(), instruction.rd());
            const auto imm = instruction.imm();

            if (!as.try_cmp(a, imm)) {
              const auto b = load_immediate_or_zero(RegisterAllocation::a_reg, imm);
              as.cmp(a, b);
            }

            as.cset(dest, instruction_type == IT::Sltiu ? a64::Condition::UnsignedLess
                                                        : a64::Condition::Less);

            register_cache.unlock_register(a);
            register_cache.unlock_register_dirty(dest);
          }
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
        if (instruction.rd() != Register::Zero) {
          const auto [a, b, dest] =
            register_cache.lock_registers(instruction.rs1(), instruction.rs2(), instruction.rd());

          const auto a32 = cast_to_32bit(a);
          const auto b32 = cast_to_32bit(b);
          const auto dest32 = cast_to_32bit(dest);

          switch (instruction_type) {
              // clang-format off
            case IT::Add: as.add(dest, a, b); break;
            case IT::Sub: as.sub(dest, a, b); break;
            case IT::Xor: as.eor(dest, a, b); break;
            case IT::Or:  as.orr(dest, a, b); break;
            case IT::And: as.and_(dest, a, b); break;
            case IT::Sll: as.lsl(dest, a, b); break;
            case IT::Srl: as.lsr(dest, a, b); break;
            case IT::Sra: as.asr(dest, a, b); break;
            case IT::Addw: as.add(dest32, a32, b32); as.sxtw(dest, dest); break;
            case IT::Subw: as.sub(dest32, a32, b32); as.sxtw(dest, dest); break;
            case IT::Sllw: as.lsl(dest32, a32, b32); as.sxtw(dest, dest); break;
            case IT::Srlw: as.lsr(dest32, a32, b32); as.sxtw(dest, dest); break;
            case IT::Sraw: as.asr(dest32, a32, b32); as.sxtw(dest, dest); break;
              // clang-format on

            default:
              unreachable();
          }

          register_cache.unlock_registers(a, b);
          register_cache.unlock_register_dirty(dest);
        }
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
        if (instruction.rd() != Register::Zero) {
          const auto [a, b, dest] =
            register_cache.lock_registers(instruction.rs1(), instruction.rs2(), instruction.rd());
          const auto tmp = RegisterAllocation::a_reg;

          const auto a32 = cast_to_32bit(a);
          const auto b32 = cast_to_32bit(b);
          const auto dest32 = cast_to_32bit(dest);
          const auto tmp32 = cast_to_32bit(tmp);

          switch (instruction_type) {
              // clang-format off
            case IT::Mul:   as.mul(dest, a, b); break;
            case IT::Mulw:  as.mul(dest32, a32, b32);  as.sxtw(dest, dest); break;
            case IT::Div:   as.sdiv(dest, a, b); break;
            case IT::Divw:  as.sdiv(dest32, a32, b32); as.sxtw(dest, dest); break;
            case IT::Divu:  as.udiv(dest, a, b); break;
            case IT::Divuw: as.udiv(dest32, a32, b32); as.sxtw(dest, dest); break;
              // clang-format on

            case IT::Rem: {
              as.sdiv(tmp, a, b);
              as.msub(dest, tmp, b, a);
              break;
            }
            case IT::Remu: {
              as.udiv(tmp, a, b);
              as.msub(dest, tmp, b, a);
              break;
            }
            case IT::Remw: {
              as.sdiv(tmp32, a32, b32);
              as.msub(dest32, tmp32, b32, a32);
              as.sxtw(dest, dest32);
              break;
            }
            case IT::Remuw: {
              as.udiv(tmp32, a32, b32);
              as.msub(dest32, tmp32, b32, a32);
              as.sxtw(dest, dest32);
              break;
            }

            default:
              unreachable();
          }

          register_cache.unlock_registers(a, b);
          register_cache.unlock_register_dirty(dest);

          break;
        }
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

      const auto continue_execution = generate_instruction(instruction);

      register_cache.finish_instruction();

      if (!continue_execution) {
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
    base_pc = pc;
    current_pc = pc;

    // We cannot use load_immediate here.
    as.macro_mov(RegisterAllocation::base_pc, int64_t(base_pc));

    generate_block(pc);
    generate_pending_exits();
  }
};

std::span<const uint32_t> jit::aarch64::generate_block_code(CodegenContext& context,
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