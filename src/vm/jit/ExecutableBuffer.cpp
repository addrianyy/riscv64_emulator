#include "ExecutableBuffer.hpp"

#include <base/Error.hpp>
#include <base/Platform.hpp>

#ifdef PLATFORM_MAC

#include <libkern/OSCacheControl.h>
#include <sys/mman.h>

static void* allocate_executable_memory(size_t size) {
  const auto p = mmap(nullptr, size, PROT_EXEC | PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
  return p != MAP_FAILED ? p : nullptr;
}

static void free_executable_memory(void* p, size_t size) {
  munmap(p, size);
}

static void unprotect_executable_memory() {
  pthread_jit_write_protect_np(0);
}

static void protect_executable_memory() {
  pthread_jit_write_protect_np(1);
}

static void flush_instruction_cache(void* memory, size_t size) {
  sys_icache_invalidate(memory, size);
}

#elif defined(PLATFORM_LINUX)

#include <sys/mman.h>

static void* allocate_executable_memory(size_t size) {
  const auto p =
    mmap(nullptr, size, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p != MAP_FAILED ? p : nullptr;
}

static void free_executable_memory(void* p, size_t size) {
  munmap(p, size);
}

static void unprotect_executable_memory() {}

static void protect_executable_memory() {}

static void flush_instruction_cache(void* memory, size_t size) {
  __builtin___clear_cache(memory, reinterpret_cast<uint8_t*>(memory + size));
}

#else
#error "Unsupported platform"
#endif

using namespace vm::jit;

ExecutableBuffer::ExecutableBuffer(size_t size) : size_(size) {
  memory_ = reinterpret_cast<uint8_t*>(allocate_executable_memory(size));
  verify(memory_, "failed to allocate {} bytes of executable memory", size);
}

ExecutableBuffer::ExecutableBuffer(const void* data, size_t size) : ExecutableBuffer(size) {
  write(0, data, size);
}

ExecutableBuffer::~ExecutableBuffer() {
  free_executable_memory(memory_, size_);
}

void ExecutableBuffer::write(uintptr_t offset, const void* data, size_t size) {
  verify(offset + size <= size_, "writing out of bounds data to executable buffer");

  unprotect_executable_memory();
  std::memcpy(memory_ + offset, data, size);
  flush_instruction_cache(memory_ + offset, size);
  protect_executable_memory();
}
