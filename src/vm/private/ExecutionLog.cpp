#include "ExecutionLog.hpp"

#include <base/Log.hpp>

using namespace vm;

void ExecutionLog::print_execution_step(const RegisterState& old_state,
                                        const RegisterState& new_state) {
  std::string s;

  for (size_t i = 0; i < size_t(Register::Pc); ++i) {
    const auto reg = Register(i);

    if (old_state.get(reg) != new_state.get(reg)) {
      base::format_to(std::back_inserter(s), "{}:{:x} ", reg, new_state.get(reg));
    }
  }

  log_info("{:x} | {}", old_state.pc(), s);
}