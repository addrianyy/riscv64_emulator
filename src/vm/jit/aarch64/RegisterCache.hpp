#pragma once
#include "Registers.hpp"

#include <vm/Register.hpp>

#include <asmlib_a64/Assembler.hpp>

#include <base/containers/StaticVector.hpp>

#include <span>

namespace vm::jit::aarch64 {

class RegisterCache {
  constexpr static size_t cache_size = RegisterAllocation::cache_size;

 public:
  struct StateSnapshot {
    uint8_t registers[cache_size]{};

    static_assert(Register::Zero == Register(uint8_t{}), "Register::Zero is not default value");
  };

  struct WriteOnly {
    Register reg{};
  };

 private:
  constexpr static auto invalid_id = std::numeric_limits<uint16_t>::max();

  struct RegisterToLock {
    Register reg{};
    bool write_only = false;

    RegisterToLock(WriteOnly r) : reg(r.reg), write_only(true) {}
    RegisterToLock(Register r) : reg(r), write_only(false) {}
  };

  asmlib::a64::Assembler& as;
  uint32_t program_counter = 0;

  struct Slot {
    uint32_t last_use = 0;
    Register reg = Register::Zero;
    bool locked = false;
    bool dirty = false;
  };
  Slot slots[cache_size]{};

  uint16_t register_to_slot[32]{};
  uint16_t platform_register_to_slot[32]{};

  base::StaticVector<uint16_t, cache_size> free_slots;

  void emit_register_load(A64R target, Register source);
  void emit_register_store(Register target, A64R source);

  uint32_t acquire_cache_slot();
  void free_cache_slots(uint32_t count);

  void reserve_registers(std::span<const RegisterToLock> registers);
  void lock_registers_internal(std::span<const RegisterToLock> registers, std::span<A64R> output);

 public:
  explicit RegisterCache(asmlib::a64::Assembler& as);

  template <typename T>
  A64R lock_register(T reg) {
    const auto [locked] = lock_registers(reg);
    return locked;
  }

  template <typename... Args>
  std::array<A64R, sizeof...(Args)> lock_registers(Args... args) {
    const std::array<RegisterToLock, sizeof...(Args)> registers_to_lock{args...};

    std::array<A64R, sizeof...(Args)> output{};
    lock_registers_internal(registers_to_lock, output);

    return output;
  }

  void unlock_register(A64R reg, bool make_dirty = false);
  void unlock_register_dirty(A64R reg) { return unlock_register(reg, true); }

  template <typename... Args>
  void unlock_registers(Args... args) {
    (unlock_register(args), ...);
  }

  StateSnapshot take_state_snapshot() const;
  void flush_registers(const StateSnapshot& snapshot);
  void flush_current_registers() { flush_registers(take_state_snapshot()); }

  void finish_instruction();
};

}  // namespace vm::jit::aarch64