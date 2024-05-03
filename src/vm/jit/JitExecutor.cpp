#include "JitExecutor.hpp"
#include "JitCodeDump.hpp"

#include <base/Error.hpp>
#include <base/Log.hpp>

#include <vm/Instruction.hpp>
#include <vm/private/ExecutionLog.hpp>

#include <asmlib_a64/Assembler.hpp>

using namespace vm;

using A64R = a64::Register;

enum class JitExitReasonInternal {
  UnalignedPc,
  OutOfBoundsPc,
  InstructionFetchFault,
  BlockNotGenerated,
  SingleStep,
  UndefinedInstruction,
  UnsupportedInstruction,
  MemoryReadFault,
  MemoryWriteFault,
  Ecall,
  Ebreak,
};

namespace vm {
struct JitCodegenContext {
  struct Exit {
    a64::Label label;
    JitExitReasonInternal reason{};
    A64R pc_register{A64R::Xzr};
    uint64_t pc_value{};
  };
  std::vector<Exit> pending_exits;
  a64::Assembler assembler;

  JitCodegenContext& prepare() {
    pending_exits.clear();
    assembler.clear();

    return *this;
  }
};
}  // namespace vm

struct JitTrampolineBlock {
  uint64_t register_state;
  uint64_t memory_base;
  uint64_t memory_size;
  uint64_t block_base;
  uint64_t max_executable_pc;
  uint64_t code_base;
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

struct CodeGenerator {
  a64::Assembler& as;
  const Memory& memory;

  size_t max_code_blocks{};
  bool single_step{};
  bool multithreaded_code_buffer{};

  uint64_t base_pc{};
  uint64_t current_pc{};

  std::vector<JitCodegenContext::Exit>& pending_exits;

  constexpr static auto register_state_reg = A64R::X0;
  constexpr static auto memory_base_reg = A64R::X1;
  constexpr static auto memory_size_reg = A64R::X2;
  constexpr static auto block_base_reg = A64R::X3;
  constexpr static auto max_executable_pc_reg = A64R::X4;
  constexpr static auto code_base_reg = A64R::X5;
  constexpr static auto base_pc_reg = A64R::X6;

  constexpr static auto a_reg = A64R::X10;
  constexpr static auto b_reg = A64R::X11;
  constexpr static auto c_reg = A64R::X12;

  constexpr static auto exit_reason_reg = A64R::X0;
  constexpr static auto exit_pc_reg = A64R::X1;

  void load_register(A64R target, Register reg) {
    verify(reg != Register::Pc, "cannot load PC as GPR");

    if (reg == Register::Zero) {
      as.mov(target, 0);
    } else {
      as.ldr(target, register_state_reg, uint32_t(reg) * sizeof(uint64_t));
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
      as.str(source, register_state_reg, uint32_t(reg) * sizeof(uint64_t));
    }
  }

  void load_immediate(A64R target, int64_t immediate) {
    const auto pc_offset = int64_t(immediate) - int64_t(base_pc);
    const auto max_pc_delta = int64_t(std::numeric_limits<int16_t>::max());

    // Roughly estimate if it's worth trying to use pc-based immediates.
    if (pc_offset >= -max_pc_delta && pc_offset <= max_pc_delta) {
      if (as.try_add_i(target, base_pc_reg, pc_offset)) {
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

  void load_register_with_offset(A64R value_reg, A64R scratch_reg, Register reg, int64_t offset) {
    if (reg == Register::Zero) {
      return load_immediate(value_reg, offset);
    }

    load_register(value_reg, reg);

    if (offset != 0) {
      if (!as.try_add_i(value_reg, value_reg, offset)) {
        load_immediate(scratch_reg, offset);
        as.add(value_reg, value_reg, scratch_reg);
      }
    }
  }

  void generate_exit(JitExitReasonInternal reason) { generate_exit(reason, current_pc); }
  void generate_exit(JitExitReasonInternal reason, uint64_t pc) {
    load_immediate_u(exit_reason_reg, uint64_t(reason));
    load_immediate_u(exit_pc_reg, pc);
    as.ret();
  }
  void generate_exit(JitExitReasonInternal reason, A64R pc) {
    load_immediate_u(exit_reason_reg, uint64_t(reason));
    as.mov(exit_pc_reg, pc);
    as.ret();
  }

  void add_pending_exit(a64::Label label, JitExitReasonInternal reason) {
    add_pending_exit(label, reason, current_pc);
  }
  void add_pending_exit(a64::Label label, JitExitReasonInternal reason, uint64_t pc) {
    pending_exits.push_back({
      .label = label,
      .reason = reason,
      .pc_value = pc,
    });
  }
  void add_pending_exit(a64::Label label, JitExitReasonInternal reason, A64R pc) {
    pending_exits.push_back({
      .label = label,
      .reason = reason,
      .pc_register = pc,
    });
  }
  void generate_pending_exits() {
    for (const auto& pending_exit : pending_exits) {
      as.insert_label(pending_exit.label);

      if (pending_exit.pc_register != A64R::Xzr) {
        generate_exit(pending_exit.reason, pending_exit.pc_register);
      } else {
        generate_exit(pending_exit.reason, pending_exit.pc_value);
      }
    }
  }

  void generate_memory_translate(A64R address_reg, uint64_t access_size_log2, bool write) {
    const auto fault_label = as.allocate_label();

    // Make sure that address is aligned otherwise the bound check later won't be accurate.
    if (access_size_log2 > 0) {
      as.tst(address_reg, (1 << access_size_log2) - 1);
      as.b(a64::Condition::NotZero, fault_label);
    }

    // Check if address >= memory_size. We don't need to account for the access size because we
    // have already checked for alignment.
    as.cmp(address_reg, memory_size_reg);
    as.b(a64::Condition::UnsignedGreaterEqual, fault_label);

    // Calculate real memory address.
    as.add(address_reg, memory_base_reg, address_reg);

    add_pending_exit(fault_label, write ? JitExitReasonInternal::MemoryWriteFault
                                        : JitExitReasonInternal::MemoryReadFault);
  }

  a64::Label generate_validated_branch(A64R block_offset_reg) {
    // Load the 32 bit code offset from block translation table.
    if (multithreaded_code_buffer) {
      as.add(block_offset_reg, block_base_reg, block_offset_reg);
      as.ldar(cast_to_32bit(block_offset_reg), block_offset_reg);
    } else {
      as.ldr(cast_to_32bit(block_offset_reg), block_base_reg, block_offset_reg);
    }

    const auto code_offset_reg = block_offset_reg;

    // Exit the VM if the block isn't generated yet.
    const auto no_block_label = as.allocate_label();
    as.cbz(code_offset_reg, no_block_label);

    // Jump to the block.
    as.add(code_offset_reg, code_base_reg, code_offset_reg);
    as.br(code_offset_reg);

    return no_block_label;
  }

  void generate_static_branch(uint64_t target_pc, A64R scratch_reg) {
    const auto block = target_pc / 4;

    // We can statically handle some error conditions.
    if ((target_pc & 3) != 0) {
      return generate_exit(JitExitReasonInternal::UnalignedPc);
    }
    if (block >= max_code_blocks) {
      return generate_exit(JitExitReasonInternal::OutOfBoundsPc);
    }

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(JitExitReasonInternal::SingleStep, target_pc);
    } else {
      // Calculate the memory offset from `block_base`.
      load_immediate_u(scratch_reg, block * 4);

      const auto exit_label = generate_validated_branch(scratch_reg);
      add_pending_exit(exit_label, JitExitReasonInternal::BlockNotGenerated, target_pc);
    }
  }

  void generate_dynamic_branch(A64R target_pc, A64R scratch_reg) {
    const auto oob_label = as.allocate_label();
    const auto unaligned_label = as.allocate_label();

    // Mask off last bit as is required by the architecture.
    as.and_(target_pc, target_pc, ~uint64_t(1));

    // Exit the VM if the address is not properly aligned.
    as.tst(target_pc, 0b11);
    as.b(a64::Condition::NotZero, unaligned_label);

    // Exit the VM if target_pc >= max_executable_pc.
    as.cmp(target_pc, max_executable_pc_reg);
    as.b(a64::Condition::UnsignedGreaterEqual, oob_label);

    if (single_step) {
      // Exit the VM to make sure that we don't execute 2 instructions when single stepping
      // (branch + 1 instruction after the branch).
      generate_exit(JitExitReasonInternal::SingleStep, target_pc);
    } else {
      const auto exit_label = generate_validated_branch(scratch_reg);
      add_pending_exit(exit_label, JitExitReasonInternal::BlockNotGenerated, target_pc);
    }

    add_pending_exit(oob_label, JitExitReasonInternal::OutOfBoundsPc, target_pc);
    add_pending_exit(unaligned_label, JitExitReasonInternal::UnalignedPc, target_pc);
  }

  bool generate_instruction(const Instruction& instruction) {
    const auto instruction_type = instruction.type();

    using IT = InstructionType;

    switch (instruction_type) {
      case IT::Lui: {
        load_immediate(a_reg, instruction.imm());
        store_register(instruction.rd(), a_reg);
      }

      case IT::Auipc: {
        load_immediate_u(a_reg, current_pc + instruction.imm());
        store_register(instruction.rd(), a_reg);
        break;
      }

      case InstructionType::Jal: {
        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          load_immediate_u(a_reg, current_pc + 4);
          store_register(rd, a_reg);
        }

        const auto target = current_pc + instruction.imm();
        generate_static_branch(target, a_reg);

        return false;
      }

      case InstructionType::Jalr: {
        load_register_with_offset(a_reg, b_reg, instruction.rs1(), instruction.imm());

        if (const auto rd = instruction.rd(); rd != Register::Zero) {
          load_immediate_u(b_reg, current_pc + 4);
          store_register(rd, b_reg);
        }

        generate_dynamic_branch(a_reg, b_reg);

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

        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto b = load_register_or_zero(b_reg, instruction.rs2());

        const auto skip_label = as.allocate_label();

        as.cmp(a, b);
        as.b(condition, skip_label);

        generate_static_branch(current_pc + instruction.imm(), a_reg);

        as.insert_label(skip_label);

        break;
      }

      case IT::Lb:
      case IT::Lh:
      case IT::Lw:
      case IT::Ld:
      case IT::Lbu:
      case IT::Lhu:
      case IT::Lwu: {
        const auto address_reg = a_reg;
        const auto scratch_reg = b_reg;
        const auto value_reg = b_reg;

        load_register_with_offset(address_reg, scratch_reg, instruction.rs1(), instruction.imm());
        generate_memory_translate(address_reg, memory_access_size_log2(instruction_type), false);

        switch (instruction_type) {
            // clang-format off
          case IT::Lb:  as.ldrsb(value_reg, address_reg, 0); break;
          case IT::Lh:  as.ldrsh(value_reg, address_reg, 0); break;
          case IT::Lw:  as.ldrsw(value_reg, address_reg, 0); break;
          case IT::Ld:  as.ldr(value_reg, address_reg, 0); break;
          case IT::Lbu: as.ldrb(value_reg, address_reg, 0); break;
          case IT::Lhu: as.ldrh(value_reg, address_reg, 0); break;
          case IT::Lwu: as.ldr(cast_to_32bit(value_reg), address_reg, 0); break;
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
        const auto address_reg = a_reg;
        const auto scratch_reg = b_reg;
        const auto value_reg = b_reg;

        load_register_with_offset(address_reg, scratch_reg, instruction.rs1(), instruction.imm());
        generate_memory_translate(address_reg, memory_access_size_log2(instruction_type), true);

        load_register(value_reg, instruction.rs2());

        switch (instruction_type) {
            // clang-format off
          case IT::Sb: as.strb(value_reg, address_reg, 0); break;
          case IT::Sh: as.strh(value_reg, address_reg, 0); break;
          case IT::Sw: as.str(cast_to_32bit(value_reg), address_reg, 0); break;
          case IT::Sd: as.str(value_reg, address_reg, 0); break;
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
        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto imm = instruction.imm();
        auto dest = c_reg;

        bool succeeded = false;

        switch (instruction_type) {
          case IT::Addi:
          case IT::Addiw: {
            if (imm == 0) {
              dest = a;
              succeeded = true;
            } else {
              if (a == a64::Register::Xzr) {
                as.macro_mov(dest, imm);
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
          const auto b = load_immediate_or_zero(b_reg, instruction.imm());

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

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Slli:
      case IT::Srli:
      case IT::Srai:
      case IT::Slliw:
      case IT::Srliw:
      case IT::Sraiw: {
        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto dest = b_reg;

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

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Slt:
      case IT::Sltu: {
        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto b = load_register_or_zero(b_reg, instruction.rs2());
        const auto dest = c_reg;

        as.cmp(a, b);
        as.cset(dest,
                instruction_type == IT::Sltu ? a64::Condition::UnsignedLess : a64::Condition::Less);

        store_register(instruction.rd(), dest);

        break;
      }

      case IT::Slti:
      case IT::Sltiu: {
        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto imm = instruction.imm();
        const auto dest = c_reg;

        if (!as.try_cmp(a, imm)) {
          const auto b = load_immediate_or_zero(b_reg, imm);
          as.cmp(a, b);
        }

        as.cset(dest, instruction_type == IT::Sltiu ? a64::Condition::UnsignedLess
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
        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto b = load_register_or_zero(b_reg, instruction.rs2());
        const auto dest = c_reg;

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
        const auto a = load_register_or_zero(a_reg, instruction.rs1());
        const auto b = load_register_or_zero(b_reg, instruction.rs2());
        const auto dest = c_reg;

        const auto a32 = cast_to_32bit(a);
        const auto b32 = cast_to_32bit(b);
        const auto dest32 = cast_to_32bit(dest);

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
            as.sdiv(dest, a, b);
            as.msub(dest, dest, b, a);
            break;
          }
          case IT::Remu: {
            as.udiv(dest, a, b);
            as.msub(dest, dest, b, a);
            break;
          }
          case IT::Remw: {
            as.sdiv(dest32, a32, b32);
            as.msub(dest32, dest32, b32, a32);
            as.sxtw(dest, dest);
            break;
          }
          case IT::Remuw: {
            as.udiv(dest32, a32, b32);
            as.msub(dest32, dest32, b32, a32);
            as.sxtw(dest, dest);
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
        generate_exit(JitExitReasonInternal::UnsupportedInstruction);
        return false;
      }

      case IT::Fence: {
        break;
      }

      case IT::Ecall: {
        generate_exit(JitExitReasonInternal::Ecall);
        return false;
      }
      case IT::Ebreak: {
        generate_exit(JitExitReasonInternal::Ebreak);
        return false;
      }
      case IT::Undefined: {
        generate_exit(JitExitReasonInternal::UndefinedInstruction);
        return false;
      }

      default:
        fatal_error("unknown instruction {}", instruction);
    }

    return true;
  }

  void generate_block() {
    // We cannot use load_immediate here.
    as.macro_mov(base_pc_reg, int64_t(base_pc));

    while (true) {
      uint32_t instruction_encoded;
      if (!memory.read(current_pc, instruction_encoded)) {
        generate_exit(JitExitReasonInternal::InstructionFetchFault);
        break;
      }

      Instruction instruction{instruction_encoded};
      if (!generate_instruction(instruction)) {
        break;
      }

      current_pc += 4;

      if (single_step) {
        generate_exit(JitExitReasonInternal::SingleStep);
        break;
      }
    }

    generate_pending_exits();
  }
};

void* JitExecutor::generate_code(const Memory& memory, uint64_t pc) {
  auto& context = codegen_context->prepare();

  CodeGenerator code_generator{
    .as = context.assembler,
    .memory = memory,
    .max_code_blocks = code_buffer->max_block_count(),

#ifdef PRINT_EXECUTION_LOG
    .single_step = true,
#else
    .single_step = false,
#endif

    .multithreaded_code_buffer = code_buffer->type() == JitCodeBuffer::Type::Multithreaded,

    .base_pc = pc,
    .current_pc = pc,

    .pending_exits = context.pending_exits,
  };

  code_generator.generate_block();

  const auto instructions = code_generator.as.assembled_instructions();
  const auto instruction_bytes = cast_instructions_to_bytes(instructions);

  if (code_dump) {
    code_dump->write(pc, instruction_bytes);
  }

  if (false) {
    log_debug("generated code for {:x}: {} instructions...", pc, instructions.size());
  }

  return code_buffer->insert(pc, instruction_bytes);
}

void JitExecutor::generate_trampoline() {
  using CG = CodeGenerator;

  auto& as = codegen_context->prepare().assembler;

  constexpr auto block_reg = A64R::X10;

  as.mov(block_reg, A64R::X0);
  as.stp(block_reg, A64R::X30, A64R::Sp, -16, a64::Writeback::Pre);

  as.ldr(CG::register_state_reg, block_reg, offsetof(JitTrampolineBlock, register_state));
  as.ldr(CG::memory_base_reg, block_reg, offsetof(JitTrampolineBlock, memory_base));
  as.ldr(CG::memory_size_reg, block_reg, offsetof(JitTrampolineBlock, memory_size));
  as.ldr(CG::block_base_reg, block_reg, offsetof(JitTrampolineBlock, block_base));
  as.ldr(CG::max_executable_pc_reg, block_reg, offsetof(JitTrampolineBlock, max_executable_pc));
  as.ldr(CG::code_base_reg, block_reg, offsetof(JitTrampolineBlock, code_base));

  as.ldr(A64R::X10, block_reg, offsetof(JitTrampolineBlock, entrypoint));
  as.blr(A64R::X10);

  as.ldp(block_reg, A64R::X30, A64R::Sp, 16, a64::Writeback::Post);

  as.str(CG::exit_reason_reg, block_reg, offsetof(JitTrampolineBlock, exit_reason));
  as.str(CG::exit_pc_reg, block_reg, offsetof(JitTrampolineBlock, exit_pc));

  as.ret();

  trampoline_fn =
    code_buffer->insert_standalone(cast_instructions_to_bytes(as.assembled_instructions()));
}

JitExecutor::JitExecutor(std::shared_ptr<JitCodeBuffer> code_buffer)
    : code_buffer(std::move(code_buffer)), codegen_context(std::make_unique<JitCodegenContext>()) {
  generate_trampoline();

  if (true) {
    code_dump = std::make_unique<JitCodeDump>("jit_dump.bin");
  }
}

JitExecutor::~JitExecutor() = default;

JitExitReason JitExecutor::run(Memory& memory, Cpu& cpu) {
  JitExitReasonInternal exit_reason{};

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
      .entrypoint = uint64_t(code),
    };

    reinterpret_cast<void (*)(JitTrampolineBlock*)>(trampoline_fn)(&trampoline_block);

    cpu.set_reg(Register::Pc, trampoline_block.exit_pc);

#ifdef PRINT_EXECUTION_LOG
    ExecutionLog::print_execution_step(previous_register_state, cpu.register_state());
#endif

    exit_reason = JitExitReasonInternal(trampoline_block.exit_reason);
    if (exit_reason != JitExitReasonInternal::BlockNotGenerated &&
        exit_reason != JitExitReasonInternal::SingleStep) {
      break;
    }
  }

  using I = JitExitReasonInternal;
  using O = JitExitReason;

  switch (exit_reason) {
      // clang-format off
    case I::UnalignedPc: return O::UnalignedPc;
    case I::OutOfBoundsPc: return O::OutOfBoundsPc;
    case I::InstructionFetchFault: return O::InstructionFetchFault;
    case I::UndefinedInstruction: return O::UndefinedInstruction;
    case I::UnsupportedInstruction: return O::UnsupportedInstruction;
    case I::MemoryReadFault: return O::MemoryReadFault;
    case I::MemoryWriteFault: return O::MemoryWriteFault;
    case I::Ecall: return O::Ecall;
    case I::Ebreak: return O::Ebreak;
      // clang-format on

    default:
      unreachable();
  }
}
