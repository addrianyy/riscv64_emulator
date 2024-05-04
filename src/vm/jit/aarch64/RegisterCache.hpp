#pragma once
#include "Registers.hpp"

#include <vm/Register.hpp>

#include <asmlib_a64/Assembler.hpp>

#include <base/containers/StaticVector.hpp>

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

  struct Slot {
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

  uint32_t acquire_free_slot();

  bool try_reserve_register(Register reg);

 public:
  explicit RegisterCache(a64::Assembler& as);

  A64R lock_register(Register reg);

  template <typename... Args>
  std::array<A64R, sizeof...(Args)> lock_registers(Args... args) {
    (try_reserve_register(args), ...);
    return std::array<A64R, sizeof...(Args)>{lock_register(args)...};
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