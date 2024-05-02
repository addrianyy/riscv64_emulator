#define _CRT_SECURE_NO_WARNINGS

#include <base/Error.hpp>
#include <base/File.hpp>

#include <cstdio>

using namespace base;

template <typename T>
static T read_file_internal(const std::string& path, const char* mode) {
  File file(path, mode, File::OpenFlags::NoBuffering);
  verify(file, "opening file `{}` for reading failed", path);

  file.seek(File::SeekOrigin::End, 0);
  const auto file_size = file.tell();
  file.seek(File::SeekOrigin::Set, 0);

  T buffer;
  buffer.resize(file_size + 1);

  const auto read_size = file.read(buffer.data(), buffer.size());
  verify(!file.error(), "reading file failed");
  verify(read_size <= file_size, "read unexpected amount of data");

  buffer.resize(read_size);

  return buffer;
}

static void write_file_internal(const std::string& path,
                                const char* mode,
                                const void* buffer,
                                size_t buffer_size) {
  File file(path, mode, File::OpenFlags::NoBuffering);
  verify(file, "opening file `{}` for writing failed", path);

  const auto size = file.write(buffer, buffer_size);
  verify(size == buffer_size, "couldn't write the whole data");
}

std::vector<uint8_t> File::read_binary_file(const std::string& path) {
  return read_file_internal<std::vector<uint8_t>>(path, "rb");
}

std::string File::read_text_file(const std::string& path) {
  return read_file_internal<std::string>(path, "r");
}

void File::write_binary_file(const std::string& path, const void* data, size_t size) {
  write_file_internal(path, "wb", data, size);
}

void File::write_text_file(const std::string& path, std::string_view contents) {
  write_file_internal(path, "w", contents.data(), contents.size());
}

File::File(const std::string& path, const char* mode, OpenFlags flags) {
  fp = std::fopen(path.c_str(), mode);

  if ((flags & OpenFlags::NoBuffering) == OpenFlags::NoBuffering) {
    if (fp) {
      std::setbuf(fp, nullptr);
    }
  }
}

File::~File() {
  close();
}

File::File(File&& other) noexcept {
  fp = other.fp;
  other.fp = nullptr;
}

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    if (fp) {
      std::fclose(fp);
    }
    fp = other.fp;
    other.fp = nullptr;
  }

  return *this;
}

bool File::opened() const {
  return fp != nullptr;
}

bool File::error() const {
  return std::ferror(fp) != 0;
}

bool File::eof() const {
  return std::feof(fp) != 0;
}

int64_t File::tell() const {
  return std::ftell(fp);
}

void File::seek(SeekOrigin origin, int64_t offset) {
  int c_origin;
  switch (origin) {
    case SeekOrigin::Current:
      c_origin = SEEK_CUR;
      break;

    case SeekOrigin::Set:
      c_origin = SEEK_SET;
      break;

    case SeekOrigin::End:
      c_origin = SEEK_END;
      break;
  }

  std::fseek(fp, offset, c_origin);
}

void File::flush() {
  std::fflush(fp);
}

size_t File::read(void* buffer, size_t size) {
  return std::fread(buffer, 1, size, fp);
}

size_t File::write(const void* buffer, size_t size) {
  return std::fwrite(buffer, 1, size, fp);
}

void File::close() {
  if (fp) {
    std::fclose(fp);
    fp = nullptr;
  }
}