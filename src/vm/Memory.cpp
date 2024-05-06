#include "Memory.hpp"

#include <cstring>

using namespace vm;

Memory::Memory(size_t size) : size_(size) {
  const auto block_count = (size + sizeof(uint64_t) - 1) / sizeof(uint64_t);
  contents_ = std::make_unique<uint64_t[]>(block_count * 2);
  permissions_ = contents_.get() + block_count;
}

bool Memory::read(uint64_t address, void* data, size_t size) const {
  if (address > size_ || address + size > size_) {
    return false;
  }
  std::memcpy(data, contents() + address, size);
  return true;
}

bool Memory::write(uint64_t address, const void* data, size_t size) {
  if (address > size_ || address + size > size_) {
    return false;
  }
  std::memcpy(contents() + address, data, size);
  return true;
}

bool Memory::read(uint64_t address, MemoryFlags required_flags, void* data, size_t size) const {
  if (address > size_ || address + size > size_) {
    return false;
  }
  if (!verify_permissions(address, size, required_flags)) {
    return false;
  }

  std::memcpy(data, contents() + address, size);
  return true;
}

bool Memory::write(uint64_t address, MemoryFlags required_flags, const void* data, size_t size) {
  if (address > size_ || address + size > size_) {
    return false;
  }
  if (!verify_permissions(address, size, required_flags)) {
    return false;
  }

  std::memcpy(contents() + address, data, size);
  return true;
}

bool Memory::verify_permissions(uint64_t address, size_t size, MemoryFlags required_flags) const {
  if (address > size_ || address + size > size_) {
    return false;
  }

  const auto p = permissions() + address;
  for (size_t i = 0; i < size; ++i) {
    if ((p[i] & required_flags) != required_flags) {
      return false;
    }
  }

  return true;
}

bool Memory::set_permissions(uint64_t address, size_t size, MemoryFlags flags) {
  if (address > size_ || address + size > size_) {
    return false;
  }

  const auto p = const_cast<MemoryFlags*>(permissions()) + address;
  std::memset(p, uint8_t(flags), size);

  return true;
}
