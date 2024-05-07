#include "CodeDump.hpp"

#include <base/Error.hpp>

using namespace vm::jit;

CodeDump::CodeDump(const std::string& path, Architecture architecture) : file(path, "wb") {
  verify(file, "failed to open jit dump for writing ({})", path);

  const auto magic = uint32_t(0xab773acf);
  verify(file.write(&magic, sizeof(magic)) == sizeof(magic), "writing jit dump magic failed");

  const auto architecture_c = uint32_t(architecture);
  verify(file.write(&architecture_c, sizeof(architecture_c)) == sizeof(architecture_c),
         "writing jit architecture failed");
}

void CodeDump::write(uint64_t pc, std::span<const uint8_t> code) {
  const uint64_t address = pc;
  const uint64_t size = code.size();

  verify(file.write(&address, sizeof(address)) == sizeof(address),
         "writing jit block address failed");
  verify(file.write(&size, sizeof(size)) == sizeof(size), "writing jit block size failed");
  verify(file.write(code.data(), code.size()) == code.size(), "writing jit block contents");

  file.flush();
}
