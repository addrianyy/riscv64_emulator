#pragma once
#include <cstdint>
#include <memory>

namespace vm {

class Memory {
  size_t size_;
  std::unique_ptr<uint64_t[]> contents_;

 public:
  explicit Memory(size_t size);

  size_t size() const { return size_; }
  uint8_t* contents() { return reinterpret_cast<uint8_t*>(contents_.get()); }
  const uint8_t* contents() const { return reinterpret_cast<const uint8_t*>(contents_.get()); }

  bool read(uint64_t address, void* data, size_t size) const;
  bool write(uint64_t address, const void* data, size_t size);

  template <typename T>
  bool read(uint64_t address, T& value) const {
    return read(address, &value, sizeof(T));
  }

  template <typename T>
  bool write(uint64_t address, const T& value) {
    return write(address, &value, sizeof(T));
  }
};

}  // namespace vm