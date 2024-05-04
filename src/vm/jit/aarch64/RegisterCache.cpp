#include "RegisterCache.hpp"

#include <base/Error.hpp>

#include <bit>

using namespace vm::jit::aarch64;

void RegisterCache::emit_register_load(A64R target, Register source) {
  as.ldr(target, RegisterAllocation::register_state, uint32_t(source) * sizeof(uint64_t));
}

void RegisterCache::emit_register_store(Register target, A64R source) {
  as.str(source, RegisterAllocation::register_state, uint32_t(target) * sizeof(uint64_t));
}

uint32_t RegisterCache::acquire_cache_slot() {
  verify(!free_slots.empty(), "cannot acquire slot: register cache is full");
  const auto slot_id = free_slots.back();
  free_slots.pop_back();
  return slot_id;
}

void RegisterCache::free_cache_slots(uint32_t count) {
  base::StaticVector<uint16_t, cache_size> available_slots;
  for (size_t i = 0; i < std::size(slots); ++i) {
    if (!slots[i].locked) {
      available_slots.push_back(uint16_t(i));
    }
  }

  verify(available_slots.size() >= count,
         "not enough available register cache slots to evict {} registers", count);

  std::sort(available_slots.begin(), available_slots.end(), [&](uint16_t ia, uint16_t ib) {
    const auto& a = slots[ia];
    const auto& b = slots[ib];
    return a.last_use > b.last_use;
  });

  for (size_t i = 0; i < count; ++i) {
    const auto slot_id = available_slots.back();
    available_slots.pop_back();

    auto& slot = slots[slot_id];

    emit_register_store(slot.reg, RegisterAllocation::cache[slot_id]);

    register_to_slot[size_t(slot.reg)] = invalid_id;
    slot = Slot{};

    free_slots.push_back(slot_id);
  }
}

void RegisterCache::reserve_register(Register reg) {
  if (reg == Register::Zero) {
    return;
  }

  if (const auto slot_id = register_to_slot[size_t(reg)]; slot_id != invalid_id) {
    slots[slot_id].locked = true;
  } else if (free_slots.empty()) {
    free_cache_slots(1);
  }
}

void RegisterCache::reserve_registers(std::span<const Register> registers) {
  uint64_t missing_registers_set = 0;

  for (const auto reg : registers) {
    if (reg == Register::Zero) {
      continue;
    }

    if (const auto slot_id = register_to_slot[size_t(reg)]; slot_id != invalid_id) {
      slots[slot_id].locked = true;
    } else {
      verify(uint32_t(reg) < 64, "register number too large");
      missing_registers_set |= uint64_t(1) << uint32_t(reg);
    }
  }

  const auto missing_registers_count = std::popcount(missing_registers_set);
  if (missing_registers_count > free_slots.size()) {
    const auto missing_slots = missing_registers_count - free_slots.size();
    free_cache_slots(missing_slots);
  }
}

A64R RegisterCache::lock_reserved_register(Register reg) {
  if (reg == Register::Zero) {
    return A64R::Xzr;
  }

  if (const auto slot_id = register_to_slot[size_t(reg)]; slot_id != invalid_id) {
    auto& slot = slots[slot_id];
    slot.locked = true;
    slot.last_use = program_counter;

    return RegisterAllocation::cache[slot_id];
  }

  const auto slot_id = acquire_cache_slot();

  auto& slot = slots[slot_id];
  slot.reg = reg;
  slot.locked = true;
  slot.last_use = program_counter;

  register_to_slot[size_t(reg)] = slot_id;

  const auto platform_register = RegisterAllocation::cache[slot_id];

  emit_register_load(platform_register, reg);

  return platform_register;
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
  reserve_register(reg);
  return lock_reserved_register(reg);
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
  program_counter++;
}