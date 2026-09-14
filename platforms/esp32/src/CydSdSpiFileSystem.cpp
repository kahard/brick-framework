#include "brick/platform/esp32/wroom32/CydSdSpiFileSystem.h"

namespace brick::platform::esp32::wroom32
{
namespace
{

    class CydSdFile final : public brick::interfaces::storage::IFile
    {
    public:
        CydSdFile(std::unique_ptr<brick::interfaces::storage::IFile> file, CydSpi3PinMux& pin_mux)
            : file_(std::move(file)), pin_mux_(pin_mux)
        {
        }

        ~CydSdFile() override { pin_mux_.select_sd(); }

        std::size_t read(void* buffer, std::size_t size, std::size_t count) override
        {
            return pin_mux_.select_sd() ? file_->read(buffer, size, count) : 0;
        }

        std::size_t write(const void* buffer, std::size_t size, std::size_t count) override
        {
            return pin_mux_.select_sd() ? file_->write(buffer, size, count) : 0;
        }

        bool seek(long offset, int origin) override { return pin_mux_.select_sd() && file_->seek(offset, origin); }

    private:
        std::unique_ptr<brick::interfaces::storage::IFile> file_;
        CydSpi3PinMux&                                     pin_mux_;
    };

}  // namespace

CydSdSpiFileSystem::CydSdSpiFileSystem(SdSpiFileSystemConfig config, CydSpi3PinMux& pin_mux)
    : pin_mux_(pin_mux), filesystem_(config)
{
}

CydSdSpiFileSystem::~CydSdSpiFileSystem()
{
    unmount();
}

bool CydSdSpiFileSystem::mount()
{
    return pin_mux_.select_sd() && filesystem_.mount();
}

void CydSdSpiFileSystem::unmount()
{
    pin_mux_.select_sd();
    filesystem_.unmount();
}

bool CydSdSpiFileSystem::mounted() const
{
    return filesystem_.mounted();
}

bool CydSdSpiFileSystem::probe(const char* path)
{
    return pin_mux_.select_sd() && filesystem_.probe(path);
}

std::vector<std::string> CydSdSpiFileSystem::list_files(const char* path)
{
    if (!pin_mux_.select_sd())
        return {};
    return filesystem_.list_files(path);
}

std::unique_ptr<brick::interfaces::storage::IFile> CydSdSpiFileSystem::open(const char* path, const char* mode)
{
    if (!pin_mux_.select_sd())
        return nullptr;
    std::unique_ptr<brick::interfaces::storage::IFile> file = filesystem_.open(path, mode);
    if (!file)
        return nullptr;
    return std::make_unique<CydSdFile>(std::move(file), pin_mux_);
}

}  // namespace brick::platform::esp32::wroom32
