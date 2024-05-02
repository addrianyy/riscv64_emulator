#pragma once
#include <base/ClassTraits.hpp>

#include <cstddef>
#include <cstdint>

class ExecutableBuffer {
  uint8_t* memory_{};
  size_t size_{};

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(ExecutableBuffer)

  explicit ExecutableBuffer(size_t size);
  ExecutableBuffer(const void* data, size_t size);
  ~ExecutableBuffer();

  void write(uintptr_t offset, const void* data, size_t size);

  void* address(uintptr_t offset = 0) const { return memory_ + offset; }
  size_t size() const { return size_; }
};