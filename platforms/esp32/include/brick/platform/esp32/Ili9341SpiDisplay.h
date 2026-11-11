#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "brick/interfaces/display/IDisplayDevice.h"
#include "brick/platform/esp32/EspStatus.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace brick::platform::esp32
{

struct Ili9341SpiDisplayConfig
{
    std::uint16_t     width                 = 320;
    std::uint16_t     height                = 240;
    spi_host_device_t spi_host              = SPI2_HOST;
    gpio_num_t        sclk_gpio             = GPIO_NUM_NC;
    gpio_num_t        mosi_gpio             = GPIO_NUM_NC;
    gpio_num_t        miso_gpio             = GPIO_NUM_NC;
    gpio_num_t        cs_gpio               = GPIO_NUM_NC;
    gpio_num_t        dc_gpio               = GPIO_NUM_NC;
    gpio_num_t        backlight_gpio        = GPIO_NUM_NC;
    bool              backlight_active_high = true;
    int               spi_mode              = 0;
    std::uint32_t     spi_clock_hz          = 40'000'000;
    bool              bgr                   = true;
    std::uint8_t      madctl                = 0x28;
};

/// ILI9341 on an SPI bus with DMA.
///
/// Pixels are copied into two internal DMA buffers, swapped to the panel's
/// MSB-first byte order on the way (two pixels per 32-bit word); one buffer is
/// filled while the other is sent. draw_buffer() returns as soon as the
/// caller's pixels are copied: the last transfers run on in the background
/// while the caller prepares the next area, and every later operation first
/// collects them. fill_rect() prepares one buffer with the color and sends it
/// repeatedly, without copying per transfer.
class Ili9341SpiDisplay final : public brick::interfaces::display::IDisplayDevice
{
public:
    explicit Ili9341SpiDisplay(Ili9341SpiDisplayConfig config);
    ~Ili9341SpiDisplay() override;
    Status                                          begin() override;
    brick::interfaces::display::DisplaySize         size() const override;
    brick::interfaces::display::PixelFormat         pixel_format() const override;
    brick::interfaces::display::DisplayCapabilities capabilities() const override;
    Status set_rotation(brick::interfaces::display::Rotation rotation) override;
    Status draw_buffer(brick::interfaces::display::DisplayRect        area,
                       const brick::interfaces::display::PixelBuffer& buffer) override;
    Status fill_rect(brick::interfaces::display::DisplayRect area, std::uint16_t color) override;

private:
    Status begin_spi_();
    Status transmit_(bool data, const std::uint8_t* bytes, std::size_t length);
    Status send_command_(std::uint8_t command, const std::uint8_t* args = nullptr, std::size_t length = 0);
    Status send_pixels_(const brick::interfaces::display::PixelBuffer& buffer, std::size_t row_bytes);
    Status send_repeated_(const std::uint8_t* data, std::size_t chunk_bytes, std::size_t total_bytes);
    Status reserve_transaction_();
    Status queue_transaction_(const std::uint8_t* data, std::size_t length);
    Status collect_transaction_();
    Status drain_();
    bool   inside_(brick::interfaces::display::DisplayRect area) const;
    Status set_address_window_(std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t height);
    Status initialize_panel_();

    Ili9341SpiDisplayConfig      config_;
    spi_device_handle_t          spi_device_         = nullptr;
    bool                         spi_ready_          = false;
    bool                         started_            = false;
    static constexpr std::size_t kPixelTransferBytes = 4 * 1024;
    // One buffer is filled while the other is sent.
    static constexpr std::size_t kPixelBufferCount = 2;
    static_assert(kPixelTransferBytes % 4 == 0, "a transfer holds whole 32-bit words of RGB565 pixels");
    alignas(4) std::array<std::array<std::uint8_t, kPixelTransferBytes>, kPixelBufferCount> pixel_tx_buffers_{};
    // Members, not locals: the SPI driver reads a queued descriptor until it
    // is collected, also after the call that queued it has returned.
    std::array<spi_transaction_t, kPixelBufferCount> pixel_transactions_{};
    // Transactions queued and not yet collected; the oldest one uses
    // next_transaction_ when all are in flight.
    std::size_t                  queued_           = 0;
    std::size_t                  next_transaction_ = 0;
    static constexpr const char* TAG               = "brick_ili9341_spi";
};

}  // namespace brick::platform::esp32
