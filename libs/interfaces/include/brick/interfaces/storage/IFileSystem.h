#pragma once

#include <cstddef>

#include "brick/Status.h"
#include "brick/interfaces/storage/IFile.h"

namespace brick::interfaces::storage
{

/// One directory entry returned by IFileSystem::list_files().
struct FileEntry
{
    static constexpr std::size_t kMaxNameLength = 63;
    /// File name without the directory, truncated to kMaxNameLength characters.
    char name[kMaxNameLength + 1] = {};

    void set_name(const char* text)
    {
        std::size_t length = 0;
        for (; text != nullptr && text[length] != '\0' && length < kMaxNameLength; ++length)
            name[length] = text[length];
        name[length] = '\0';
    }
};

class FileHandle;

/// File system on any medium. Nothing here allocates: list_files() fills a
/// caller-provided array and open() binds a caller-owned FileHandle to one of
/// the file objects the file system keeps for that purpose.
class IFileSystem
{
public:
    virtual ~IFileSystem() = default;
    virtual Status mount() = 0;

    /// Writes the regular files of directory path into entries (at most
    /// capacity of them) and returns how many there are in total, which can be
    /// more than capacity. Pass nullptr and 0 to only count them.
    virtual std::size_t list_files(const char* path, FileEntry* entries, std::size_t capacity) = 0;

    /// Opens path with an fopen()-style mode and binds file to it, closing what
    /// file held before. Status::busy when every file object of the file system
    /// is in use; other codes when the file cannot be opened.
    virtual Status open(const char* path, const char* mode, FileHandle& file) = 0;

protected:
    friend class FileHandle;
    /// Closes a file returned by open() and makes its object available again.
    virtual void close(IFile& file) = 0;
};

/// Caller-owned handle to an open file; closes it when destroyed. It must not
/// outlive the file system that opened it, and files must be closed before
/// that file system is unmounted.
class FileHandle final
{
public:
    FileHandle() = default;
    ~FileHandle() { close(); }
    FileHandle(const FileHandle&)            = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    bool     is_open() const { return file_ != nullptr; }
    explicit operator bool() const { return is_open(); }

    /// The open file; only valid while is_open().
    IFile* operator->() const { return file_; }
    IFile& operator*() const { return *file_; }

    void close()
    {
        if (file_ != nullptr)
            owner_->close(*file_);
        owner_ = nullptr;
        file_  = nullptr;
    }

    /// Called by IFileSystem::open() implementations.
    void attach(IFileSystem& owner, IFile& file)
    {
        close();
        owner_ = &owner;
        file_  = &file;
    }

private:
    IFileSystem* owner_ = nullptr;
    IFile*       file_  = nullptr;
};

}  // namespace brick::interfaces::storage
