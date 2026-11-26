#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"
#include "brick/platform/stm32/h7/Gpio.h"
#include "stm32h7xx_hal.h"

namespace brick::platform::stm32::h7
{

/// SDRAM timings in SDRAM clock cycles (FMC_SDRAM_TimingTypeDef).
struct SdramTiming
{
    std::uint32_t load_to_active    = 2;
    std::uint32_t exit_self_refresh = 8;
    std::uint32_t self_refresh      = 5;
    std::uint32_t row_cycle         = 7;
    std::uint32_t write_recovery    = 2;
    std::uint32_t rp_delay          = 3;
    std::uint32_t rcd_delay         = 3;
};

/// The FMC kernel clock from PLL2 R. The SDRAM clock is it divided by
/// clock_period.
struct SdramClock
{
    std::uint32_t pll2_m           = 2;
    std::uint32_t pll2_n           = 80;
    std::uint32_t pll2_p           = 2;
    std::uint32_t pll2_q           = 2;
    std::uint32_t pll2_r           = 2;
    std::uint32_t pll2_input_range = RCC_PLL2VCIRANGE_2;
};

struct Stm32SdramConfig
{
    const AlternatePin* pins      = nullptr;  ///< every FMC SDRAM pin
    std::size_t         pin_count = 0;

    std::uint32_t bank            = FMC_SDRAM_BANK2;
    std::uint32_t column_bits     = FMC_SDRAM_COLUMN_BITS_NUM_9;
    std::uint32_t row_bits        = FMC_SDRAM_ROW_BITS_NUM_13;
    std::uint32_t data_width      = FMC_SDRAM_MEM_BUS_WIDTH_16;
    std::uint32_t internal_banks  = FMC_SDRAM_INTERN_BANKS_NUM_4;
    std::uint32_t cas_latency     = FMC_SDRAM_CAS_LATENCY_3;
    std::uint32_t clock_period    = FMC_SDRAM_CLOCK_PERIOD_2;
    std::uint32_t read_pipe_delay = FMC_SDRAM_RPIPE_DELAY_1;
    SdramTiming   timing;
    SdramClock    clock;
    /// Mode register value: burst length, CAS latency, write burst mode.
    std::uint32_t mode_register  = 0x231;
    std::uint32_t refresh_cycles = 8;
    /// Refresh timer count: refresh period / rows * SDRAM clock - 20.
    std::uint32_t refresh_count = 605;

    std::uintptr_t base_address = 0xD0000000;
    std::size_t    size_bytes   = 0;
};

/// SDRAM on the FMC. begin() configures the pins and the FMC clock, runs the
/// SDRAM start sequence and checks that the memory keeps a test pattern.
class Stm32Sdram final
{
public:
    explicit Stm32Sdram(Stm32SdramConfig config) : config_(config) {}

    Status begin();

    void*       data() const { return reinterpret_cast<void*>(config_.base_address); }
    std::size_t size() const { return config_.size_bytes; }

private:
    Status send_command_(std::uint32_t mode, std::uint32_t refresh_number, std::uint32_t mode_register);
    Status check_() const;

    Stm32SdramConfig    config_;
    SDRAM_HandleTypeDef handle_{};
    bool                started_ = false;
};

}  // namespace brick::platform::stm32::h7
