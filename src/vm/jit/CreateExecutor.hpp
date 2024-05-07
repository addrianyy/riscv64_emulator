#pragma once
#include "CodeBuffer.hpp"
#include "CodeDump.hpp"
#include "Executor.hpp"

namespace vm::jit {
std::unique_ptr<Executor> create_arch_specific_executor(std::shared_ptr<CodeBuffer> code_buffer);
}