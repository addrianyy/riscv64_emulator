#pragma once
#include "Registers.hpp"

#include <vm/Register.hpp>

#include <asmlib_a64/Assembler.hpp>

#include <base/containers/StaticVector.hpp>

#include <span>

namespace vm::aarch64 {

class RegisterCache {
  constexpr static size_t cache_size = RegisterAllocation::cache_size;

 public:
  struct StateSnapshot {
    uint8_t registers[cache_size]{};

    static_assert(Register::Zero == Register(uint8_t{}), "Register::Zero is not default value");
  };

 private:
  constexpr static auto invalid_id = std::numeric_limits<uint16_t>::max();

  a64::Assembler& as;
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

  void reserve_register(Register reg);
  void reserve_registers(std::span<const Register> registers);

  A64R lock_reserved_register(Register reg);

 public:
  explicit RegisterCache(a64::Assembler& as);

  A64R lock_register(Register reg);

  template <typename... Args>
  std::array<A64R, sizeof...(Args)> lock_registers(Args... args) {
    const std::array<Register, sizeof...(Args)> all_registers{args...};

    reserve_registers(all_registers);

    return std::array<A64R, sizeof...(Args)>{lock_reserved_register(args)...};
  }

  void unlock_register(A64R reg, bool make_dirty = false);
  void unlock_register_dirty(A64R reg) { return unlock_register(reg, true); }

  template <typename... Args>
  void unlock_registers(Args... args) {
    (unlock_register(args), ...);
  }

  StateSnapshot take_state_snapshot() const;
  void flush_registers(const StateSnapshot& snapshot);

  void finish_instruction();
};

}  // namespace vm::aarch64