#pragma once
#include <asmlib_x64/Assembler.hpp>

#include "Exit.hpp"
#include "Registers.hpp"

namespace vm::jit::x64 {

struct CodegenContext {
  asmlib::x64::Assembler assembler;

  struct Exit {
    asmlib::x64::Label label;
    ArchExitReason reason{};
    X64R pc_register{X64R::Rsp};
    uint64_t pc_value{};
  };
  std::vector<Exit> pending_exits;

  CodegenContext& prepare() {
    assembler.clear();
    pending_exits.clear();

    return *this;
  }
};

}  // namespace vm::jit::x64