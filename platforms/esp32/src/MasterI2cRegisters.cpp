#include "brick/platform/esp32/EspStatus.h"
#include "brick/platform/esp32/audio/Es8311Codec.h"
#include "driver/i2c_master.h"

// The i2c_master driver; a separate file so an application on the legacy
// driver does not link it (ESP-IDF aborts with both).
namespace brick::platform::esp32::audio
{

namespace
{
    constexpr int           kTimeoutMs = 50;
    constexpr std::uint32_t kSpeedHz   = 400000;
}  // namespace

MasterI2cRegisters::~MasterI2cRegisters()
{
    if (device_ != nullptr)
        i2c_master_bus_rm_device(device_);
}

Status MasterI2cRegisters::attach_()
{
    if (device_ != nullptr)
        return Status::ok;
    if (bus_ == nullptr)
        return Status::not_ready;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address  = address_;
    config.scl_speed_hz    = kSpeedHz;
    return to_status(i2c_master_bus_add_device(bus_, &config, &device_));
}

Status MasterI2cRegisters::write(std::uint8_t reg, std::uint8_t value)
{
    if (const Status status = attach_(); !is_ok(status))
        return status;
    const std::uint8_t data[2] = { reg, value };
    return to_status(i2c_master_transmit(device_, data, sizeof(data), kTimeoutMs));
}

Status MasterI2cRegisters::read(std::uint8_t reg, std::uint8_t& value)
{
    if (const Status status = attach_(); !is_ok(status))
        return status;
    return to_status(i2c_master_transmit_receive(device_, &reg, 1, &value, 1, kTimeoutMs));
}

}  // namespace brick::platform::esp32::audio
