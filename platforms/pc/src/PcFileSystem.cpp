#include "brick/platform/pc/PcFileSystem.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace brick::platform::pc
{

PcFile::~PcFile()
{
    if (file_ != nullptr)
        std::fclose(file_);
}

std::size_t PcFile::read(void* buffer, std::size_t size, std::size_t count)
{
    return std::fread(buffer, size, count, file_);
}

std::size_t PcFile::write(const void* buffer, std::size_t size, std::size_t count)
{
    return std::fwrite(buffer, size, count, file_);
}

bool PcFile::seek(long offset, int origin)
{
    return std::fseek(file_, offset, origin) == 0;
}

PcFileSystem::PcFileSystem(std::string root, std::string mount_point)
    : root_(std::move(root)), mount_point_(std::move(mount_point))
{
}

bool PcFileSystem::mount()
{
    std::error_code error;
    mounted_ = std::filesystem::is_directory(root_, error);
    return mounted_;
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

std::vector<std::string> PcFileSystem::list_files(const char* path)
{
    std::vector<std::string> names;
    if (!mounted_)
        return names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(host_path(path), error))
        if (entry.is_regular_file(error))
            names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

std::unique_ptr<brick::interfaces::storage::IFile> PcFileSystem::open(const char* path, const char* mode)
{
    if (!mounted_ || mode == nullptr)
        return nullptr;
    // Always use binary mode: target code expects byte-exact reads and writes.
    std::string binary_mode = mode;
    if (binary_mode.find('b') == std::string::npos)
        binary_mode += 'b';
    FILE* file = std::fopen(host_path(path).c_str(), binary_mode.c_str());
    if (file == nullptr)
        return nullptr;
    return std::make_unique<PcFile>(file);
}

}  // namespace brick::platform::pc
