#pragma once

#include <cstdint>

#include "brick/Status.h"
#include "stm32h7xx_hal.h"

// Clock tree, memory protection and caches of an STM32H7, set up once by the
// board at start, before any peripheral.
namespace brick::platform::stm32::h7
{

/// System clock from the HSE crystal through PLL1. HSE_VALUE (a compiler
/// definition the HAL reads) must match the crystal. The defaults give
/// 400 MHz from an 8 MHz crystal at voltage scale 1: AXI/AHB 200 MHz, every
/// APB 100 MHz.
struct SystemClockConfig
{
    std::uint32_t pll_m           = 1;  ///< 8 MHz / 1 = 8 MHz into the PLL
    std::uint32_t pll_n           = 100;
    std::uint32_t pll_p           = 2;  ///< system clock: 800 MHz VCO / 2
    std::uint32_t pll_q           = 4;
    std::uint32_t pll_r           = 2;
    std::uint32_t pll_input_range = RCC_PLL1VCIRANGE_3;  ///< 8-16 MHz
    std::uint32_t voltage_scale   = PWR_REGULATOR_VOLTAGE_SCALE1;
    std::uint32_t flash_latency   = FLASH_LATENCY_2;
};

/// Supplies the core from the LDO, sets the voltage scale, starts the HSE
/// and PLL1 and moves the system clock to it. HAL_Init() comes first.
Status configure_system_clock(const SystemClockConfig& config);

/// Memory protection regions: external RAM (FMC SDRAM) as normal memory,
/// write-through, so the CPU may access it unaligned and every write reaches
/// the RAM, where the LCD controller and DMA read it, without a cache clean.
/// The unused FMC NOR/SRAM bank is closed: speculative reads there stall
/// the bus. The rest of the memory map keeps its default attributes.
void configure_memory_protection(std::uint32_t external_ram_base, std::uint32_t external_ram_mpu_size);

/// Instruction and data caches on.
void enable_caches();

}  // namespace brick::platform::stm32::h7
