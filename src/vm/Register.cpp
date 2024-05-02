#include "Register.hpp"
#include "private/InstructionDisplay.hpp"

std::string_view vm::detail::register_name(Register reg) {
  return InstructionDisplay::register_name(reg);
}
