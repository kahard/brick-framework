#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace brick::interfaces::storage {

class IFile {
 public:
  virtual ~IFile() = default;
  virtual std::size_t read(void* buffer, std::size_t size, std::size_t count) = 0;
  virtual bool seek(long offset, int origin) = 0;
};

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;
  virtual bool mount() = 0;
  virtual std::vector<std::string> list_files(const char* path) = 0;
  virtual std::unique_ptr<IFile> open(const char* path, const char* mode) = 0;
};

}  // namespace brick::interfaces::storage
