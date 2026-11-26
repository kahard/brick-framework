#include "brick/platform/stm32/h7/Stm32Sdram.h"

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr std::uint32_t kCommandTimeoutMs = 100;
    constexpr std::uint32_t kPowerUpDelayMs   = 1;  ///< at least 100 us of clock before the first command
    constexpr std::size_t   kCheckStepBytes   = 4096;
    constexpr std::uint32_t kCheckSeed        = 0x9E3779B9;
}  // namespace

Status Stm32Sdram::begin()
{
    if (started_)
        return Status::ok;
    if (config_.pins == nullptr || config_.pin_count == 0 || config_.size_bytes == 0)
        return Status::invalid_argument;

    configure(config_.pins, config_.pin_count, GPIO_SPEED_FREQ_VERY_HIGH);

    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    clock.FmcClockSelection    = RCC_FMCCLKSOURCE_PLL2;
    clock.PLL2.PLL2M           = config_.clock.pll2_m;
    clock.PLL2.PLL2N           = config_.clock.pll2_n;
    clock.PLL2.PLL2P           = config_.clock.pll2_p;
    clock.PLL2.PLL2Q           = config_.clock.pll2_q;
    clock.PLL2.PLL2R           = config_.clock.pll2_r;
    clock.PLL2.PLL2RGE         = config_.clock.pll2_input_range;
    clock.PLL2.PLL2VCOSEL      = RCC_PLL2VCOWIDE;
    clock.PLL2.PLL2FRACN       = 0;
    if (const HAL_StatusTypeDef result = HAL_RCCEx_PeriphCLKConfig(&clock); result != HAL_OK)
        return to_status(result);
    __HAL_RCC_FMC_CLK_ENABLE();

    handle_.Instance                = FMC_SDRAM_DEVICE;
    handle_.Init.SDBank             = config_.bank;
    handle_.Init.ColumnBitsNumber   = config_.column_bits;
    handle_.Init.RowBitsNumber      = config_.row_bits;
    handle_.Init.MemoryDataWidth    = config_.data_width;
    handle_.Init.InternalBankNumber = config_.internal_banks;
    handle_.Init.CASLatency         = config_.cas_latency;
    handle_.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    handle_.Init.SDClockPeriod      = config_.clock_period;
    handle_.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
    handle_.Init.ReadPipeDelay      = config_.read_pipe_delay;

    FMC_SDRAM_TimingTypeDef timing{};
    timing.LoadToActiveDelay    = config_.timing.load_to_active;
    timing.ExitSelfRefreshDelay = config_.timing.exit_self_refresh;
    timing.SelfRefreshTime      = config_.timing.self_refresh;
    timing.RowCycleDelay        = config_.timing.row_cycle;
    timing.WriteRecoveryTime    = config_.timing.write_recovery;
    timing.RPDelay              = config_.timing.rp_delay;
    timing.RCDDelay             = config_.timing.rcd_delay;
    if (const HAL_StatusTypeDef result = HAL_SDRAM_Init(&handle_, &timing); result != HAL_OK)
        return to_status(result);

    if (const Status status = send_command_(FMC_SDRAM_CMD_CLK_ENABLE, 1, 0); !is_ok(status))
        return status;
    HAL_Delay(kPowerUpDelayMs);
    const Status sequence[] = {
        send_command_(FMC_SDRAM_CMD_PALL, 1, 0),
        send_command_(FMC_SDRAM_CMD_AUTOREFRESH_MODE, config_.refresh_cycles, 0),
        send_command_(FMC_SDRAM_CMD_LOAD_MODE, 1, config_.mode_register),
    };
    for (const Status status : sequence)
        if (!is_ok(status))
            return status;
    if (const HAL_StatusTypeDef result = HAL_SDRAM_ProgramRefreshRate(&handle_, config_.refresh_count);
        result != HAL_OK)
        return to_status(result);
    if (const Status status = check_(); !is_ok(status))
        return status;
    started_ = true;
    return Status::ok;
}

Status Stm32Sdram::send_command_(std::uint32_t mode, std::uint32_t refresh_number, std::uint32_t mode_register)
{
    FMC_SDRAM_CommandTypeDef command{};
    command.CommandMode   = mode;
    command.CommandTarget = config_.bank == FMC_SDRAM_BANK2 ? FMC_SDRAM_CMD_TARGET_BANK2 : FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber      = refresh_number;
    command.ModeRegisterDefinition = mode_register;
    return to_status(HAL_SDRAM_SendCommand(&handle_, &command, kCommandTimeoutMs));
}

/// Writes a word every kCheckStepBytes over the whole memory, then reads them
/// back: a wrong row, column or bank count, or a broken address line, makes
/// words overwrite each other. Runs before the data cache is on.
Status Stm32Sdram::check_() const
{
    // brick-rules: allow(volatile) the test has to reach the SDRAM, not a register copy of the value
    volatile std::uint32_t* const memory = reinterpret_cast<volatile std::uint32_t*>(config_.base_address);
    const std::size_t             step   = kCheckStepBytes / sizeof(std::uint32_t);
    const std::size_t             words  = config_.size_bytes / sizeof(std::uint32_t);
    for (std::size_t index = 0; index < words; index += step)
        memory[index] = static_cast<std::uint32_t>(index) * kCheckSeed;
    for (std::size_t index = 0; index < words; index += step)
        if (memory[index] != static_cast<std::uint32_t>(index) * kCheckSeed)
            return Status::error;
    return Status::ok;
}

}  // namespace brick::platform::stm32::h7
