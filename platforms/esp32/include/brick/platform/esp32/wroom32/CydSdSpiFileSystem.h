#pragma once

#include <memory>

#include "brick/interfaces/storage/IRemovableFileSystem.h"
#include "brick/platform/esp32/SdSpiFileSystem.h"
#include "brick/platform/esp32/wroom32/CydSpi3PinMux.h"

namespace brick::platform::esp32::wroom32
{

class CydSdSpiFileSystem final : public brick::interfaces::storage::IRemovableFileSystem
{
public:
    CydSdSpiFileSystem(SdSpiFileSystemConfig config, CydSpi3PinMux& pin_mux);
    ~CydSdSpiFileSystem() override;

    bool                                               mount() override;
    void                                               unmount() override;
    bool                                               mounted() const override;
    bool                                               probe(const char* path) override;
    std::vector<std::string>                           list_files(const char* path) override;
    std::unique_ptr<brick::interfaces::storage::IFile> open(const char* path, const char* mode) override;

private:
    CydSpi3PinMux&  pin_mux_;
    SdSpiFileSystem filesystem_;
};

}  // namespace brick::platform::esp32::wroom32
