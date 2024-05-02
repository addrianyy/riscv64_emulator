#include "JitCodeDump.hpp"

#include <base/Error.hpp>

using namespace vm;

JitCodeDump::JitCodeDump(const std::string& path) : file(path, "wb") {
  verify(file, "failed to open jit dump for writing ({})", path);
}

void JitCodeDump::write(uint64_t pc, std::span<const uint8_t> code) {
  const uint64_t address = pc;
  const uint64_t size = code.size();

  verify(file.write(&address, sizeof(address)) == sizeof(address),
         "writing jit block address failed");
  verify(file.write(&size, sizeof(size)) == sizeof(size), "writing jit block size failed");
  verify(file.write(code.data(), code.size()) == code.size(), "writing jit block contents");

  file.flush();
}