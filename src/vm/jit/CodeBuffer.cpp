#include "CodeBuffer.hpp"
#include "CodeDump.hpp"

#include <base/Error.hpp>

using namespace vm::jit;

static CodeDump::Architecture code_dump_architecture() {
#if defined(VM_JIT_X64)
  return CodeDump::Architecture::X64;
#elif defined(VM_JIT_AARCH64)
  return CodeDump::Architecture::AArch64;
#else
  fatal_error("cannot enable jit dumping to file: unknown code architecture")
#endif
}

uint32_t CodeBuffer::allocate_executable_memory(std::span<const uint8_t> code) {
  constexpr auto code_alignment = uint64_t(16);

  const auto start_offset = (next_free_offset + code_alignment - 1) & ~(code_alignment - 1);
  const auto end_offset = start_offset + code.size();

  verify(end_offset <= executable_buffer.size(), "out of executable memory in the jit storage");

  executable_buffer.write(start_offset, code.data(), code.size());
  next_free_offset = end_offset;

  return start_offset;
}

CodeBuffer::CodeBuffer(Flags flags, size_t size, size_t max_executable_guest_address)
    : flags_(flags), executable_buffer(size), next_free_offset(16) {
  max_blocks = (max_executable_guest_address + block_size - 1) / block_size;
  block_to_offset = std::make_unique<std::atomic_uint32_t[]>(max_blocks);
}
CodeBuffer::~CodeBuffer() = default;

void CodeBuffer::dump_code_to_file(const std::string& path) {
  std::unique_lock lock(mutex);
  verify(!code_dump, "JIT code buffer has dumping to file already enabled");
  code_dump = std::make_unique<CodeDump>(path, code_dump_architecture());
}

void* CodeBuffer::get(uint64_t guest_address) const {
  if ((guest_address & (block_size - 1)) != 0) {
    return nullptr;
  }

  const auto block = guest_address / block_size;
  const auto offset = block_to_offset[block].load(std::memory_order::acquire);

  return offset ? executable_buffer.address(offset) : nullptr;
}

void* CodeBuffer::insert(uint64_t guest_address, std::span<const uint8_t> code) {
  verify((guest_address & (block_size - 1)) == 0, "guest address is misaligned");

  std::unique_lock lock(mutex);

  if (const auto p = get(guest_address)) {
    return p;
  }

  const auto offset = allocate_executable_memory(code);
  const auto allocation = executable_buffer.address(offset);

  const auto block = guest_address / block_size;
  block_to_offset[block].store(offset, std::memory_order::release);

  if (code_dump) {
    code_dump->write(guest_address, code);
  }

  return allocation;
}

void* CodeBuffer::insert_standalone(std::span<const uint8_t> code) {
  std::unique_lock lock(mutex);

  return executable_buffer.address(allocate_executable_memory(code));
}
