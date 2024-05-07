#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>

#include <base/EnumBitOperations.hpp>

#include "ExecutableBuffer.hpp"

namespace vm::jit {

class CodeDump;

class CodeBuffer {
 public:
  enum class Flags {
    None = 0,
    Multithreaded = (1 << 0),
    SkipPermissionChecks = (1 << 1),
  };

 private:
  constexpr static size_t block_size = 4;

  Flags flags_;

  std::unique_ptr<std::atomic_uint32_t[]> block_to_offset;
  size_t max_blocks{};

  ExecutableBuffer executable_buffer;
  size_t next_free_offset{};

  std::unique_ptr<CodeDump> code_dump;

  mutable std::mutex mutex;

  uint32_t allocate_executable_memory(std::span<const uint8_t> code);

 public:
  CodeBuffer(Flags flags, size_t size, size_t max_executable_guest_address);
  ~CodeBuffer();

  void dump_code_to_file(const std::string& path);

  void* get(uint64_t guest_address) const;
  void* insert(uint64_t guest_address, std::span<const uint8_t> code);
  void* insert_standalone(std::span<const uint8_t> code);

  Flags flags() const { return flags_; }
  size_t max_block_count() const { return max_blocks; }

  const std::atomic_uint32_t* block_translation_table() const { return block_to_offset.get(); }
  const void* code_buffer_base() const { return executable_buffer.address(0); }
};

}  // namespace vm::jit

IMPLEMENT_ENUM_BIT_OPERATIONS(vm::jit::CodeBuffer::Flags)