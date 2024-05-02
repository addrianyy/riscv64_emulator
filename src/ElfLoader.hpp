#pragma once
#include <span>
#include <string>

#include <vm/Memory.hpp>

class ElfLoader {
 public:
  struct Image {
    uint64_t base{};
    uint64_t size{};
    uint64_t entrypoint{};
  };

  static Image load(const std::string& file_path, vm::Memory& memory);
  static Image load(std::span<const uint8_t> binary, vm::Memory& memory);
};