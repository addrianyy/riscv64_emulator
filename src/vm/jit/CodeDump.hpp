#pragma once
#include <cstdint>
#include <span>
#include <string>

#include <base/File.hpp>

namespace vm::jit {

class CodeDump {
  base::File file;

 public:
  explicit CodeDump(const std::string& path);

  void write(uint64_t pc, std::span<const uint8_t> code);
};

}  // namespace vm::jit