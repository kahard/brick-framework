#include "brick/platform/esp32/EspFirmwareUpdate.h"

#include <cstdio>

#include "brick/platform/esp32/EspStatus.h"
#include "esp_app_desc.h"
#include "esp_system.h"

namespace brick::platform::esp32
{

Status EspFirmwareUpdate::begin(std::uint32_t size)
{
    if (running_)
        return Status::busy;
    target_ = esp_ota_get_next_update_partition(nullptr);
    if (target_ == nullptr)
        return Status::not_supported;
    if (size == 0 || size > target_->size)
        return Status::no_memory;
    if (const esp_err_t error = esp_ota_begin(target_, OTA_WITH_SEQUENTIAL_WRITES, &handle_); error != ESP_OK)
        return to_status(error);
    size_    = size;
    written_ = 0;
    running_ = true;
    return Status::ok;
}

Status EspFirmwareUpdate::write(const std::uint8_t* data, std::size_t size, std::size_t& written)
{
    written = 0;
    if (!running_)
        return Status::not_ready;
    if (size > size_ - written_)
    {
        abort();
        return Status::invalid_argument;
    }
    if (const esp_err_t error = esp_ota_write(handle_, data, size); error != ESP_OK)
    {
        abort();
        return to_status(error);
    }
    written_ += static_cast<std::uint32_t>(size);
    written = size;
    return Status::ok;
}

Status EspFirmwareUpdate::finish()
{
    if (!running_)
        return Status::not_ready;
    if (written_ != size_)
    {
        abort();
        return Status::invalid_argument;
    }
    running_ = false;
    // Checks the image; the handle is gone after it either way.
    if (esp_ota_end(handle_) != ESP_OK)
        return Status::error;
    return to_status(esp_ota_set_boot_partition(target_));
}

void EspFirmwareUpdate::abort()
{
    if (running_)
        (void)esp_ota_abort(handle_);
    running_ = false;
}

Status EspFirmwareUpdate::restart()
{
    esp_restart();
}

const char* EspFirmwareUpdate::description() const
{
    if (description_[0] == '\0')
    {
        const esp_app_desc_t* const program = esp_app_get_description();
        (void)std::snprintf(description_, sizeof(description_), "%s %s %s %02x%02x%02x%02x", program->version,
                            program->date, program->time, program->app_elf_sha256[0], program->app_elf_sha256[1],
                            program->app_elf_sha256[2], program->app_elf_sha256[3]);
    }
    return description_;
}

bool EspFirmwareUpdate::on_trial() const
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK
           && state == ESP_OTA_IMG_PENDING_VERIFY;
}

Status EspFirmwareUpdate::confirm()
{
    return to_status(esp_ota_mark_app_valid_cancel_rollback());
}

const char* EspFirmwareUpdate::slot() const
{
    const esp_partition_t* const partition = esp_ota_get_running_partition();
    return partition != nullptr ? partition->label : "";
}

}  // namespace brick::platform::esp32
