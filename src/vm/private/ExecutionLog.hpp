#pragma once
#include <vm/RegisterState.hpp>

namespace vm {

class ExecutionLog {
 public:
  static void print_execution_step(const RegisterState& old_state, const RegisterState& new_state);
};

}  // namespace vm