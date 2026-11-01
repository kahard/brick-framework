#pragma once

#include "brick/Status.h"
#include "esp_err.h"

namespace brick::platform::esp32
{

/// The Status closest to an ESP-IDF error code.
constexpr Status to_status(esp_err_t error)
{
    switch (error)
    {
        case ESP_OK:
            return Status::ok;
        case ESP_ERR_TIMEOUT:
            return Status::timeout;
        case ESP_ERR_NO_MEM:
            return Status::no_memory;
        case ESP_ERR_INVALID_ARG:
        case ESP_ERR_INVALID_SIZE:
            return Status::invalid_argument;
        case ESP_ERR_INVALID_STATE:
            return Status::not_ready;
        case ESP_ERR_NOT_FOUND:
            return Status::not_found;
        case ESP_ERR_NOT_SUPPORTED:
            return Status::not_supported;
        default:
            return Status::error;
    }
}

}  // namespace brick::platform::esp32
