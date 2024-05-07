#include "CreateExecutor.hpp"

#include <base/Platform.hpp>

using namespace vm;
using namespace vm::jit;

#ifdef VM_JIT_AARCH64

#include "aarch64/Executor.hpp"

std::unique_ptr<Executor> jit::create_arch_specific_executor(
  std::shared_ptr<CodeBuffer> code_buffer) {
  return std::make_unique<aarch64::Executor>(std::move(code_buffer));
}

#elif defined(VM_JIT_X64)

#include "x64/Executor.hpp"

std::unique_ptr<Executor> jit::create_arch_specific_executor(
  std::shared_ptr<CodeBuffer> code_buffer) {
#ifdef PLATFORM_WINDOWS
  const auto abi = x64::Abi::windows();
#elif defined(PLATFORM_LINUX)
  const auto abi = x64::Abi::systemv();
#elif defined(PLATFORM_MAC)
  const auto abi = x64::Abi::systemv();
#else
#define JIT_UNUSPPORTED_PLATFORM
#endif

#ifndef JIT_UNUSPPORTED_PLATFORM
  return std::make_unique<x64::Executor>(std::move(code_buffer), abi);
#else
  return nullptr;
#endif
}

#else

std::unique_ptr<Executor> jit::create_arch_specific_executor(
  std::shared_ptr<CodeBuffer> code_buffer,
  std::unique_ptr<CodeDump> code_dump) {
  return nullptr;
}

#endif
