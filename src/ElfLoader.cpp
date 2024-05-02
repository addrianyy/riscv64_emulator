#include "ElfLoader.hpp"

#include <base/Error.hpp>
#include <base/File.hpp>

class BinaryFileView {
  std::span<const uint8_t> data;

 public:
  explicit BinaryFileView(std::span<const uint8_t> data) : data(data) {}

  template <typename T>
  T read(uint64_t offset) const {
    const auto end = offset + sizeof(T);
    verify(end <= data.size(), "reading out of bounds");

    return *(const T*)(data.data() + offset);
  }

  uint8_t read8(uint64_t offset) const { return read<uint8_t>(offset); }
  uint16_t read16(uint64_t offset) const { return read<uint16_t>(offset); }
  uint32_t read32(uint64_t offset) const { return read<uint32_t>(offset); }
  uint64_t read64(uint64_t offset) const { return read<uint64_t>(offset); }

  BinaryFileView slice(uint64_t offset, size_t size) {
    const auto end = offset + size;
    verify(end <= data.size(), "reading out of bounds");

    return BinaryFileView(data.subspan(offset, size));
  }

  const uint8_t* raw() const { return data.data(); }
};

ElfLoader::Image ElfLoader::load(const std::string& file_path, vm::Memory& memory) {
  const auto file = base::File::read_binary_file(file_path);
  return load(file, memory);
}

ElfLoader::Image ElfLoader::load(std::span<const uint8_t> binary, vm::Memory& memory) {
  BinaryFileView elf(binary);

  verify(elf.read32(0x00) == 'FLE\x7F', "image has invalid ELF magic");
  verify(elf.read8(0x04) == 2, "image is not 64 bit");
  verify(elf.read8(0x05) == 1, "image is not little endian");
  verify(elf.read16(0x10) == 2, "image is not executable file");

  const auto entrypoint = elf.read64(0x18);
  const auto ph_offset = elf.read64(0x20);
  const auto phe_size = elf.read16(0x36);
  const auto phe_count = elf.read16(0x38);

  verify(phe_size == 0x38, "unexpected image program header entry size");
  verify(entrypoint != 0, "image has no entrypoint");

  uint64_t base_address = 0;
  uint64_t end_address = 0;

  for (uint32_t i = 0; i < phe_count; ++i) {
    const auto ph = elf.slice(ph_offset + i * phe_size, phe_size);

    // Skip non-loadable sections.
    const auto type = ph.read32(0x00);
    if (type != 1) {
      continue;
    }

    const auto flags = ph.read32(0x04);

    const auto file_offset = ph.read64(0x08);
    const auto memory_address = ph.read64(0x10);

    const auto file_size = ph.read64(0x20);
    const auto memory_size = ph.read64(0x28);

    // As there is no paging at this point we don't care about flags.
    (void)flags;

    if (base_address == 0) {
      base_address = memory_address;

      verify(base_address != 0, "image base address is 0");
      verify((base_address & 0xfff) == 0, "image base address is not 4K aligned");
    }

    end_address = std::max(end_address, memory_address + memory_size);

    const auto segment_data_size = std::min(file_size, memory_size);
    const auto segment_data = elf.slice(file_offset, segment_data_size);

    if (segment_data_size == 0) {
      continue;
    }

    const auto write_success = memory.write(memory_address, segment_data.raw(), segment_data_size);
    verify(write_success, "writing segment {:x} (size {:x} failed)", memory_address,
           segment_data_size);
  }

  const auto size = end_address - base_address;
  const auto aligned_size = (size + 0xfff) & ~uint64_t(0xfff);

  return Image{
    .base = base_address,
    .size = aligned_size,
    .entrypoint = entrypoint,
  };
}
