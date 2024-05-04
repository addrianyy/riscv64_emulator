#include "RegisterCache.hpp"

#include <base/Error.hpp>

using namespace vm::aarch64;

void RegisterCache::emit_register_load(A64R target, Register source) {
  as.ldr(target, RegisterAllocation::register_state, uint32_t(source) * sizeof(uint64_t));
}

void RegisterCache::emit_register_store(Register target, A64R source) {
  as.str(source, RegisterAllocation::register_state, uint32_t(target) * sizeof(uint64_t));
}

uint32_t RegisterCache::acquire_free_slot() {
  verify(!free_slots.empty(), "no free registers");
  const auto s = free_slots.back();
  free_slots.pop_back();
  return s;
}

bool RegisterCache::try_reserve_register(Register reg) {
  if (reg != Register::Zero) {
    return true;
  }

  const auto slot_id = register_to_slot[size_t(reg)];
  if (slot_id != invalid_id) {
    slots[slot_id].locked = true;
  }

  return false;
}

RegisterCache::RegisterCache(a64::Assembler& as) : as(as) {
  for (auto& slot : register_to_slot) {
    slot = invalid_id;
  }
  for (auto& slot : platform_register_to_slot) {
    slot = invalid_id;
  }

  for (size_t i = 0; i < cache_size; ++i) {
    const auto platform_register = RegisterAllocation::cache[i];
    verify(uint32_t(platform_register) < std::size(platform_register_to_slot),
           "invalid platform register used for cache");

    platform_register_to_slot[uint32_t(platform_register)] = i;

    free_slots.push_back(i);
  }
}

A64R RegisterCache::lock_register(Register reg) {
  if (reg == Register::Zero) {
    return A64R::Xzr;
  }

  if (const auto slot_id = register_to_slot[size_t(reg)]; slot_id != invalid_id) {
    slots[slot_id].locked = true;
    return RegisterAllocation::cache[slot_id];
  }

  const auto slot_id = acquire_free_slot();

  auto& slot = slots[slot_id];
  slot.locked = true;
  slot.reg = reg;

  register_to_slot[size_t(reg)] = slot_id;

  const auto platform_register = RegisterAllocation::cache[slot_id];

  emit_register_load(platform_register, reg);

  return platform_register;
}

void RegisterCache::unlock_register(A64R reg, bool make_dirty) {
  if (reg == A64R::Xzr) {
    return;
  }

  const auto slot_id = platform_register_to_slot[size_t(reg)];
  verify(slot_id != invalid_id, "cannot unlock register that is not part of the register cache");
  slots[slot_id].locked = false;
  slots[slot_id].dirty |= make_dirty;
}

RegisterCache::StateSnapshot RegisterCache::take_state_snapshot() const {
  StateSnapshot snapshot{};

  for (size_t i = 0; i < std::size(slots); ++i) {
    auto& slot = slots[i];

    if (slot.reg == Register::Zero || !slot.dirty) {
      continue;
    }

    const auto ireg = uint32_t(slot.reg);
    verify(ireg <= uint32_t(std::numeric_limits<uint8_t>::max()), "register doesn't fit in u8");

    snapshot.registers[i] = uint8_t(ireg);
  }

  return snapshot;
}

void RegisterCache::flush_registers(const StateSnapshot& snapshot) {
  for (size_t i = 0; i < std::size(snapshot.registers); ++i) {
    const auto reg = Register(snapshot.registers[i]);
    if (reg == Register::Zero) {
      continue;
    }

    emit_register_store(reg, RegisterAllocation::cache[i]);
  }
}

void RegisterCache::finish_instruction() {
  for (const auto& slot : slots) {
    verify(!slot.locked, "register {} is locked when finishing the instruction", slot.reg);
  }
}