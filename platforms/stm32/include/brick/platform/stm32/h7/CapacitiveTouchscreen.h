#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/core/input/TouchMapper.h"
#include "brick/interfaces/display/ITouchscreen.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

struct CapacitiveTouchscreenConfig
{
    I2C_HandleTypeDef*                      i2c = nullptr;  ///< a started Stm32I2cBus
    brick::interfaces::display::DisplaySize display_size{};
    /// Raw coordinate range and orientation. A zero range means the
    /// controller's own: the display size for an FT6x36, the resolution in
    /// the configuration of a GT911.
    brick::core::input::TouchCalibration calibration{};
};

/// Capacitive touch panel with either controller a board may carry: a
/// FocalTech FT6x36 (two points) or a Goodix GT911 (five points). begin()
/// probes the bus for one of them; read() reports every finger, pressed,
/// moved and, once, released.
class CapacitiveTouchscreen final : public brick::interfaces::display::ITouchscreen
{
public:
    enum class Controller : std::uint8_t
    {
        none,
        ft6x36,
        gt911,
    };

    static constexpr std::size_t kMaxTouches = 5;

    explicit CapacitiveTouchscreen(CapacitiveTouchscreenConfig config) : config_(config) {}

    Status begin() override;
    Status read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count) override;

    Controller controller() const { return controller_; }

private:
    /// One finger as the controller reports it.
    struct RawTouch
    {
        std::uint8_t id       = 0;
        std::int16_t x        = 0;
        std::int16_t y        = 0;
        std::int16_t pressure = 0;
    };

    Status      probe_ft6x36_();
    Status      probe_gt911_(std::uint8_t address);
    Status      read_ft6x36_(std::size_t& touch_count);
    Status      read_gt911_(std::size_t& touch_count);
    std::size_t report_(std::size_t touch_count, brick::interfaces::display::TouchPoint* points, std::size_t capacity);
    Status      read_registers_(std::uint16_t reg, std::uint16_t reg_size, std::uint8_t* data, std::uint16_t length);
    Status      write_register_(std::uint16_t reg, std::uint16_t reg_size, std::uint8_t value);

    CapacitiveTouchscreenConfig                                     config_;
    brick::core::input::TouchMapper                                 mapper_{ {}, {} };
    Controller                                                      controller_ = Controller::none;
    std::uint8_t                                                    address_    = 0;
    std::array<RawTouch, kMaxTouches>                               touches_{};
    std::array<bool, kMaxTouches>                                   active_{};
    std::array<brick::interfaces::display::TouchPoint, kMaxTouches> last_points_{};
};

}  // namespace brick::platform::stm32::h7
