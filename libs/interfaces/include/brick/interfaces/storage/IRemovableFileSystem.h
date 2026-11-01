#pragma once

#include "brick/interfaces/storage/IFileSystem.h"

namespace brick::interfaces::storage
{

/// File system on removable media (SD card): it can disappear at runtime, so
/// callers check mounted(), probe() a known file and remount when needed.
class IRemovableFileSystem : public IFileSystem
{
public:
    virtual void unmount()       = 0;
    virtual bool mounted() const = 0;
    /// True when the medium is mounted and path can be read: a file with at
    /// least one byte, or a directory ("/" checks that the medium is still there).
    virtual bool probe(const char* path) = 0;
};

}  // namespace brick::interfaces::storage
