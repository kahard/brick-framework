#include "brick/platform/pc/PcFileSystem.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace brick::platform::pc
{

PcFile::~PcFile()
{
    close();
}

Status PcFile::open(const std::string& host_path, const char* mode)
{
    close();
    // Always use binary mode: target code expects byte-exact reads and writes.
    std::string binary_mode = mode;
    if (binary_mode.find('b') == std::string::npos)
        binary_mode += 'b';
    file_ = std::fopen(host_path.c_str(), binary_mode.c_str());
    if (file_ != nullptr)
        return Status::ok;
    return errno == ENOENT ? Status::not_found : Status::error;
}

void PcFile::close()
{
    if (file_ != nullptr)
        std::fclose(file_);
    file_ = nullptr;
}

std::size_t PcFile::read(void* buffer, std::size_t size, std::size_t count)
{
    return std::fread(buffer, size, count, file_);
}

std::size_t PcFile::write(const void* buffer, std::size_t size, std::size_t count)
{
    return std::fwrite(buffer, size, count, file_);
}

Status PcFile::seek(long offset, int origin)
{
    return std::fseek(file_, offset, origin) == 0 ? Status::ok : Status::invalid_argument;
}

PcFileSystem::PcFileSystem(std::string root, std::string mount_point)
    : root_(std::move(root)), mount_point_(std::move(mount_point))
{
}

Status PcFileSystem::mount()
{
    std::error_code error;
    mounted_ = std::filesystem::is_directory(root_, error);
    return mounted_ ? Status::ok : Status::not_found;
}

std::string PcFileSystem::host_path(const char* path) const
{
    std::string relative = path != nullptr ? path : "";
    if (!mount_point_.empty() && relative.compare(0, mount_point_.size(), mount_point_) == 0)
        relative.erase(0, mount_point_.size());
    while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\'))
        relative.erase(0, 1);
    return (std::filesystem::path(root_) / relative).string();
}

std::size_t PcFileSystem::list_files(const char* path, brick::interfaces::storage::FileEntry* entries,
                                     std::size_t capacity)
{
    if (!mounted_)
        return 0;
    // Sorted, so tests and simulator runs see the same order on every host.
    std::vector<std::string> names;
    std::error_code          error;
    for (const auto& entry : std::filesystem::directory_iterator(host_path(path), error))
        if (entry.is_regular_file(error))
            names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    for (std::size_t index = 0; entries != nullptr && index < names.size() && index < capacity; ++index)
        entries[index].set_name(names[index].c_str());
    return names.size();
}

bool PcFileSystem::probe(const char* path)
{
    if (!mounted_ || path == nullptr)
        return false;
    std::error_code error;
    if (std::filesystem::is_directory(host_path(path), error))
        return true;
    FILE* file = std::fopen(host_path(path).c_str(), "rb");
    if (file == nullptr)
        return false;
    unsigned char byte = 0;
    const bool    ok   = std::fread(&byte, 1, 1, file) == 1;
    std::fclose(file);
    return ok;
}

Status PcFileSystem::open(const char* path, const char* mode, brick::interfaces::storage::FileHandle& file)
{
    file.close();
    if (!mounted_)
        return Status::not_ready;
    if (mode == nullptr)
        return Status::invalid_argument;
    for (PcFile& candidate : files_)
    {
        if (candidate.is_open())
            continue;
        if (const Status status = candidate.open(host_path(path), mode); !is_ok(status))
            return status;
        file.attach(*this, candidate);
        return Status::ok;
    }
    return Status::busy;
}

void PcFileSystem::close(brick::interfaces::storage::IFile& file)
{
    static_cast<PcFile&>(file).close();
}

}  // namespace brick::platform::pc
