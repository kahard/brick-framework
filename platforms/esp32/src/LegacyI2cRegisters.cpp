#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/audio/Es8311Codec.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"

// The legacy I2C driver; a separate file so an application on the i2c_master
// driver does not link it (ESP-IDF aborts with both).
namespace brick::platform::esp32::audio
{

namespace
{
    constexpr std::uint32_t kTimeoutMs = 50;
}  // namespace

Status LegacyI2cRegisters::write(std::uint8_t reg, std::uint8_t value)
{
    const std::uint8_t data[2] = { reg, value };
    return to_status(i2c_master_write_to_device(port_, address_, data, sizeof(data), pdMS_TO_TICKS(kTimeoutMs)));
}

Status LegacyI2cRegisters::read(std::uint8_t reg, std::uint8_t& value)
{
    return to_status(i2c_master_write_read_device(port_, address_, &reg, 1, &value, 1, pdMS_TO_TICKS(kTimeoutMs)));
}

}  // namespace brick::platform::esp32::audio
