#pragma once
#include <cstdint>
#include <span>
#include <string>

#include <base/File.hpp>

namespace vm::jit {

class CodeDump {
 public:
  enum class Architecture {
    AArch64 = 1,
    X64 = 2,
  };

 private:
  base::File file;

 public:
  explicit CodeDump(const std::string& path, Architecture architecture);

  void write(uint64_t pc, std::span<const uint8_t> code);
};

}  // namespace vm::jit