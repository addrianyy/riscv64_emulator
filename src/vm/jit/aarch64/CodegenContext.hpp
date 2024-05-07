#pragma once
#include <asmlib_a64/Assembler.hpp>

#include "Exit.hpp"
#include "RegisterCache.hpp"
#include "Registers.hpp"

namespace vm::jit::aarch64 {

struct CodegenContext {
  asmlib::a64::Assembler assembler;

  struct Exit {
    asmlib::a64::Label label;
    ArchExitReason reason{};
    A64R pc_register{A64R::Xzr};
    uint64_t pc_value{};
    RegisterCache::StateSnapshot snapshot;
  };
  std::vector<Exit> pending_exits;

  CodegenContext& prepare() {
    assembler.clear();
    pending_exits.clear();

    return *this;
  }
};

}  // namespace vm::jit::aarch64