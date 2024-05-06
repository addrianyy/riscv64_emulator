#include "Memory.hpp"

#include <cstring>

using namespace vm;

Memory::Memory(size_t size) : size_(size) {
  contents_ = std::make_unique<uint64_t[]>((size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
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
