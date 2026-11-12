#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/esp32/audio/I2sPcmOutput.h"
#include "driver/gpio.h"
#include "driver/i2c_types.h"

namespace brick::platform::esp32::audio
{

/// 8-bit registers of an I2C device. The ESP-IDF legacy I2C driver and the
/// newer i2c_master one cannot be linked together, so a board picks the
/// implementation of the driver its other I2C devices use.
class II2cRegisters
{
public:
    virtual ~II2cRegisters()                                   = default;
    virtual Status write(std::uint8_t reg, std::uint8_t value) = 0;
    virtual Status read(std::uint8_t reg, std::uint8_t& value) = 0;
};

/// Over the legacy driver (driver/i2c.h) on port, which the board installed
/// (e.g. for a GT911 touch screen).
class LegacyI2cRegisters final : public II2cRegisters
{
public:
    LegacyI2cRegisters(i2c_port_t port, std::uint8_t address) : port_(port), address_(address) {}
    Status write(std::uint8_t reg, std::uint8_t value) override;
    Status read(std::uint8_t reg, std::uint8_t& value) override;

private:
    i2c_port_t   port_;
    std::uint8_t address_;
};

/// Over the i2c_master driver: a device added to bus (e.g. the bus of a
/// GSL3680 touch screen) on first use.
class MasterI2cRegisters final : public II2cRegisters
{
public:
    MasterI2cRegisters(i2c_master_bus_handle_t bus, std::uint8_t address) : bus_(bus), address_(address) {}
    ~MasterI2cRegisters() override;
    MasterI2cRegisters(const MasterI2cRegisters&)            = delete;
    MasterI2cRegisters& operator=(const MasterI2cRegisters&) = delete;

    Status write(std::uint8_t reg, std::uint8_t value) override;
    Status read(std::uint8_t reg, std::uint8_t& value) override;
    void   set_bus(i2c_master_bus_handle_t bus) { bus_ = bus; }

private:
    Status attach_();

    i2c_master_bus_handle_t bus_;
    i2c_master_dev_handle_t device_ = nullptr;
    std::uint8_t            address_;
};

/// ES8311 mono codec playing I2S (16-bit, MCLK = 256 x sample rate; 8, 16,
/// 22.05, 24, 32, 44.1 and 48 kHz) into its DAC. Volume is the DAC gain in
/// 0.5 dB steps: 100 % is -10 dB, 1 % -59.5 dB, 0 % mutes.
class Es8311Codec final : public II2sCodec
{
public:
    static constexpr std::uint8_t kAddress = 0x18;

    explicit Es8311Codec(II2cRegisters& registers) : registers_(registers) {}

    Status configure(std::uint32_t sample_rate) override;
    Status set_volume(std::uint8_t percent) override;
    Status set_muted(bool muted) override;

private:
    Status write_(std::uint8_t reg, std::uint8_t value) { return registers_.write(reg, value); }
    Status update_(std::uint8_t reg, std::uint8_t keep_mask, std::uint8_t bits);
    Status clocks_(std::uint32_t sample_rate);

    II2cRegisters& registers_;
};

}  // namespace brick::platform::esp32::audio
