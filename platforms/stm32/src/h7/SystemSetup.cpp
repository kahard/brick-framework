#include "brick/platform/stm32/h7/SystemSetup.h"

#include "brick/platform/stm32/h7/HalStatus.h"

namespace brick::platform::stm32::h7
{

namespace
{
    constexpr std::uint32_t kVoltageTimeoutMs = 100;
    constexpr std::uint32_t kFmcBank1Base     = 0x60000000;
}  // namespace

Status configure_system_clock(const SystemClockConfig& config)
{
    if (const HAL_StatusTypeDef result = HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY); result != HAL_OK)
        return to_status(result);
    __HAL_PWR_VOLTAGESCALING_CONFIG(config.voltage_scale);
    const std::uint32_t start = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
        if (HAL_GetTick() - start > kVoltageTimeoutMs)
            return Status::timeout;

    RCC_OscInitTypeDef oscillators{};
    oscillators.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillators.HSEState       = RCC_HSE_ON;
    oscillators.PLL.PLLState   = RCC_PLL_ON;
    oscillators.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    oscillators.PLL.PLLM       = config.pll_m;
    oscillators.PLL.PLLN       = config.pll_n;
    oscillators.PLL.PLLP       = config.pll_p;
    oscillators.PLL.PLLQ       = config.pll_q;
    oscillators.PLL.PLLR       = config.pll_r;
    oscillators.PLL.PLLRGE     = config.pll_input_range;
    oscillators.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    oscillators.PLL.PLLFRACN   = 0;
    if (const HAL_StatusTypeDef result = HAL_RCC_OscConfig(&oscillators); result != HAL_OK)
        return to_status(result);

    RCC_ClkInitTypeDef clocks{};
    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                       | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    clocks.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clocks.AHBCLKDivider  = RCC_HCLK_DIV2;
    clocks.APB3CLKDivider = RCC_APB3_DIV2;
    clocks.APB1CLKDivider = RCC_APB1_DIV2;
    clocks.APB2CLKDivider = RCC_APB2_DIV2;
    clocks.APB4CLKDivider = RCC_APB4_DIV2;
    return to_status(HAL_RCC_ClockConfig(&clocks, config.flash_latency));
}

void configure_memory_protection(std::uint32_t external_ram_base, std::uint32_t external_ram_mpu_size)
{
    HAL_MPU_Disable();

    MPU_Region_InitTypeDef region{};
    region.Enable           = MPU_REGION_ENABLE;
    region.Number           = MPU_REGION_NUMBER0;
    region.BaseAddress      = kFmcBank1Base;
    region.Size             = MPU_REGION_SIZE_256MB;
    region.SubRegionDisable = 0;
    region.TypeExtField     = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable      = MPU_ACCESS_SHAREABLE;
    region.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    region.Number           = MPU_REGION_NUMBER1;
    region.BaseAddress      = external_ram_base;
    region.Size             = static_cast<std::uint8_t>(external_ram_mpu_size);
    region.TypeExtField     = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable      = MPU_ACCESS_CACHEABLE;
    region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;  // cacheable, not bufferable: write-through
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void enable_caches()
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

}  // namespace brick::platform::stm32::h7
