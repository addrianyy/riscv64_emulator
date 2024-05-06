#pragma once
#include <cstdint>
#include <memory>

#include <base/EnumBitOperations.hpp>

namespace vm {

enum class MemoryFlags : uint8_t {
  None = 0,
  Read = (1 << 0),
  Write = (1 << 1),
  Execute = (1 << 2),
};

class Memory {
  size_t size_;
  std::unique_ptr<uint64_t[]> contents_;
  uint64_t* permissions_;

 public:
  explicit Memory(size_t size);

  size_t size() const { return size_; }
  uint8_t* contents() { return reinterpret_cast<uint8_t*>(contents_.get()); }
  const uint8_t* contents() const { return reinterpret_cast<const uint8_t*>(contents_.get()); }
  const MemoryFlags* permissions() const {
    return reinterpret_cast<const MemoryFlags*>(permissions_);
  }

  bool read(uint64_t address, void* data, size_t size) const;
  bool write(uint64_t address, const void* data, size_t size);

  bool read(uint64_t address, MemoryFlags required_flags, void* data, size_t size) const;
  bool write(uint64_t address, MemoryFlags required_flags, const void* data, size_t size);

  bool verify_permissions(uint64_t address, size_t size, MemoryFlags required_flags) const;
  bool set_permissions(uint64_t address, size_t size, MemoryFlags flags);

  template <typename T>
  bool read(uint64_t address, T& value) const {
    return read(address, &value, sizeof(T));
  }

  template <typename T>
  bool write(uint64_t address, const T& value) {
    return write(address, &value, sizeof(T));
  }

  template <typename T>
  bool read(uint64_t address, MemoryFlags required_flags, T& value) const {
    return read(address, required_flags, &value, sizeof(T));
  }

  template <typename T>
  bool write(uint64_t address, MemoryFlags required_flags, const T& value) {
    return write(address, required_flags, &value, sizeof(T));
  }
};

}  // namespace vm

IMPLEMENT_ENUM_BIT_OPERATIONS(vm::MemoryFlags)