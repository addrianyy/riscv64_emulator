#include "Trampoline.hpp"
#include "Utilities.hpp"

#include <ranges>
#include <unordered_set>

using namespace vm::jit;
using namespace vm::jit::aarch64;

using namespace asmlib;

class RegisterSaver {
  // Return address.
  constexpr static auto filler_reg = A64R::X30;

  a64::Assembler& as;
  std::vector<std::pair<A64R, A64R>> pairs;
  std::unordered_set<A64R> already_added;

  void push(A64R a, A64R b) { as.stp(a, b, A64R::Sp, -16, a64::Writeback::Pre); }
  void pop(A64R a, A64R b) { as.ldp(a, b, A64R::Sp, 16, a64::Writeback::Post); }

  static bool is_reserved_register(A64R reg) {
    return reg == A64R::X18 || reg == A64R::X29 || reg == A64R::Xzr;
  }

  static bool is_callee_saved(A64R reg) {
    return uint32_t(reg) >= uint32_t(A64R::X19) && uint32_t(reg) <= uint32_t(A64R::X28);
  }

 public:
  explicit RegisterSaver(a64::Assembler& as) : as(as) {
    // Always store the return address.
    add_always(A64R::X30);
  }

  RegisterSaver& add_always_one(A64R reg) {
    verify(!is_reserved_register(reg), "cannot save reserved registers");

    if (!already_added.insert(reg).second) {
      return *this;
    }

    if (!pairs.empty()) {
      auto& last_register = pairs.back().second;
      if (last_register == filler_reg) {
        last_register = reg;
        return *this;
      }
    }

    pairs.emplace_back(reg, filler_reg);

    return *this;
  }

  RegisterSaver& add_one(A64R reg) {
    verify(!is_reserved_register(reg), "cannot save reserved registers");

    if (is_callee_saved(reg)) {
      add_always_one(reg);
    }

    return *this;
  }

  template <typename... Args>
  RegisterSaver& add_always(Args... args) {
    (add_always_one(args), ...);
    return *this;
  }

  template <typename... Args>
  RegisterSaver& add(Args... args) {
    (add_one(args), ...);
    return *this;
  }

  void save() {
    for (const auto& [a, b] : pairs) {
      push(a, b);
    }
  }

  void restore() {
    for (auto [a, b] : std::ranges::reverse_view(pairs)) {
      pop(a, b);
    }
  }
};

void* aarch64::generate_trampoline(CodegenContext& context, CodeBuffer& code_buffer) {
  using RA = RegisterAllocation;

  auto& as = context.prepare().assembler;

  RegisterSaver register_saver{as};

  register_saver
    .add(RA::register_state, RA::memory_base, RA::permissions_base, RA::memory_size, RA::block_base,
         RA::max_executable_pc, RA::code_base, RA::base_pc)
    .add(RA::a_reg, RA::b_reg, RA::c_reg)
    .add(RA::exit_reason, RA::exit_pc)
    .add_always(RA::trampoline_block);

  for (const auto reg : RA::cache) {
    register_saver.add(reg);
  }

  constexpr auto tb = RA::trampoline_block;

  {
    as.mov(tb, A64R::X0);

    register_saver.save();

    as.ldr(RA::register_state, tb, offsetof(TrampolineBlock, register_state));
    as.ldr(RA::memory_base, tb, offsetof(TrampolineBlock, memory_base));
    as.ldr(RA::permissions_base, tb, offsetof(TrampolineBlock, permissions_base));
    as.ldr(RA::memory_size, tb, offsetof(TrampolineBlock, memory_size));
    as.ldr(RA::block_base, tb, offsetof(TrampolineBlock, block_base));
    as.ldr(RA::max_executable_pc, tb, offsetof(TrampolineBlock, max_executable_pc));
    as.ldr(RA::code_base, tb, offsetof(TrampolineBlock, code_base));

    as.ldr(tb, tb, offsetof(TrampolineBlock, entrypoint));
    as.blr(tb);

    register_saver.restore();

    as.str(RA::exit_reason, tb, offsetof(TrampolineBlock, exit_reason));
    as.str(RA::exit_pc, tb, offsetof(TrampolineBlock, exit_pc));
    as.ret();
  }

  return code_buffer.insert_standalone(
    utils::cast_instructions_to_bytes(as.assembled_instructions()));
}