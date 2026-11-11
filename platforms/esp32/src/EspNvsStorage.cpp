#include "brick/platform/esp32/EspNvsStorage.h"

#include <cstring>

#include "brick/platform/esp32/EspStatus.h"
#include "nvs.h"

namespace brick::platform::esp32
{

EspNvsStorage::EspNvsStorage(const char* name_space, const char* key, std::size_t size)
    : name_space_(name_space), key_(key), size_(size < kMaxSize ? size : kMaxSize)
{
    bytes_.fill(0xFF);
}

Status EspNvsStorage::begin()
{
    if (begun_)
        return Status::ok;
    nvs_handle_t handle = 0;
    esp_err_t    error  = nvs_open(name_space_, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND)
    {
        // Nothing written in this namespace yet: blank.
        begun_ = true;
        return Status::ok;
    }
    if (error != ESP_OK)
        return to_status(error);
    std::size_t length = size_;
    error              = nvs_get_blob(handle, key_, bytes_.data(), &length);
    nvs_close(handle);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND && error != ESP_ERR_NVS_INVALID_LENGTH)
        return to_status(error);
    begun_ = true;
    return Status::ok;
}

Status EspNvsStorage::read(std::uint32_t address, void* data, std::size_t size)
{
    if (data == nullptr || !fits_(address, size))
        return Status::invalid_argument;
    if (const Status status = begin(); !is_ok(status))
        return status;
    std::memcpy(data, bytes_.data() + address, size);
    return Status::ok;
}

Status EspNvsStorage::write(std::uint32_t address, const void* data, std::size_t size)
{
    if (data == nullptr || !fits_(address, size))
        return Status::invalid_argument;
    if (const Status status = begin(); !is_ok(status))
        return status;
    std::memcpy(bytes_.data() + address, data, size);
    nvs_handle_t handle = 0;
    if (const esp_err_t error = nvs_open(name_space_, NVS_READWRITE, &handle); error != ESP_OK)
        return to_status(error);
    esp_err_t error = nvs_set_blob(handle, key_, bytes_.data(), size_);
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return to_status(error);
}

bool EspNvsStorage::fits_(std::uint32_t address, std::size_t size) const
{
    return address <= size_ && size <= size_ - address;
}

}  // namespace brick::platform::esp32
