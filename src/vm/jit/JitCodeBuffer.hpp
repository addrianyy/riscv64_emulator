#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>

#include "ExecutableBuffer.hpp"

namespace vm {

class JitCodeBuffer {
  constexpr static size_t block_size = 4;

  std::unique_ptr<std::atomic_uint32_t[]> block_to_offset;
  size_t max_blocks{};

  ExecutableBuffer executable_buffer;
  size_t next_free_offset{};

  mutable std::mutex mutex;

  uint32_t allocate_executable_memory(std::span<const uint8_t> code);

 public:
  JitCodeBuffer(size_t size, size_t max_executable_guest_address);

  void* get(uint64_t guest_address) const;
  void* insert(uint64_t guest_address, std::span<const uint8_t> code);
  void* insert_standalone(std::span<const uint8_t> code);

  size_t max_block_count() const { return max_blocks; }
  const std::atomic_uint32_t* block_translation_table() const { return block_to_offset.get(); }

  void* code_buffer_base() const { return executable_buffer.address(0); }
};

}  // namespace vm