#pragma once

#include <memory>
#include <string>
#include <vector>

#include "brick/interfaces/storage/IFile.h"

namespace brick::interfaces::storage
{

class IFileSystem
{
public:
    virtual ~IFileSystem()                                                    = default;
    virtual bool                     mount()                                  = 0;
    virtual std::vector<std::string> list_files(const char* path)             = 0;
    virtual std::unique_ptr<IFile>   open(const char* path, const char* mode) = 0;
};

}  // namespace brick::interfaces::storage
