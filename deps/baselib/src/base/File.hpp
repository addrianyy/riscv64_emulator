#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include "EnumBitOperations.hpp"

namespace base {

class File {
  std::FILE* fp = nullptr;

 public:
  enum class OpenFlags {
    None = 0,
    NoBuffering = (1 << 0),
  };

  enum class SeekOrigin {
    Set,
    Current,
    End,
  };

  static std::string read_text_file(const std::string& path);
  static std::vector<uint8_t> read_binary_file(const std::string& path);

  static void write_text_file(const std::string& path, std::string_view contents);
  static void write_binary_file(const std::string& path, const void* data, size_t size);

  File() = default;
  File(const std::string& path, const char* mode, OpenFlags flags = OpenFlags::None);

  ~File();

  File(File&& other) noexcept;
  File& operator=(File&& other) noexcept;

  File(File&) = delete;
  File& operator=(File&) = delete;

  operator bool() const { return opened(); }

  bool opened() const;
  bool error() const;
  bool eof() const;

  int64_t tell() const;

  void seek(SeekOrigin origin, int64_t offset);
  void flush();

  size_t read(void* buffer, size_t size);
  size_t write(const void* buffer, size_t size);

  void close();
};

}  // namespace base

IMPLEMENT_ENUM_BIT_OPERATIONS(base::File::OpenFlags)