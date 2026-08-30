#pragma once

#include "brick/interfaces/display/AssetDescriptor.h"
#include "brick/interfaces/display/AssetStorage.h"

namespace brick::core::image
{

class AssetRepository final
{
public:
    AssetRepository(brick::interfaces::display::IAssetSource* flash = nullptr, brick::interfaces::display::IAssetSource* psram = nullptr, brick::interfaces::display::IAssetSource* usb_or_sd = nullptr)
        : flash_(flash), psram_(psram), usb_or_sd_(usb_or_sd)
    {
    }

    void                                     set_storage(brick::interfaces::display::AssetStorage storage) { storage_ = storage; }
    brick::interfaces::display::AssetStorage storage() const { return storage_; }

    brick::interfaces::display::IAssetSource* source() const
    {
        switch (storage_)
        {
            case brick::interfaces::display::AssetStorage::flash_partition:
                return flash_;
            case brick::interfaces::display::AssetStorage::psram_cache:
                return psram_;
            case brick::interfaces::display::AssetStorage::usb_or_sd:
                return usb_or_sd_;
        }
        return nullptr;
    }

    bool available() const { return source() != nullptr; }

private:
    brick::interfaces::display::IAssetSource* flash_     = nullptr;
    brick::interfaces::display::IAssetSource* psram_     = nullptr;
    brick::interfaces::display::IAssetSource* usb_or_sd_ = nullptr;
    brick::interfaces::display::AssetStorage  storage_   = brick::interfaces::display::AssetStorage::flash_partition;
};

}  // namespace brick::core::image
