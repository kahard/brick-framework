#include "brick/platform/stm32/f1/ResistiveTouchscreen.h"

namespace brick::platform::stm32::f1
{
namespace
{
constexpr std::uint16_t kX1 = GPIO_PIN_4;  // PC4, ADC2_IN14
constexpr std::uint16_t kX2 = GPIO_PIN_0;  // PB0, ADC2_IN8
constexpr std::uint16_t kY1 = GPIO_PIN_5;  // PC5, ADC2_IN15
constexpr std::uint16_t kY2 = GPIO_PIN_1;  // PB1, ADC2_IN9
}

ResistiveTouchscreen::ResistiveTouchscreen(ResistiveTouchscreenConfig config) : config_(config)
{
}

bool ResistiveTouchscreen::begin()
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ADC2_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

    adc_.Instance = ADC2;
    adc_.Init.ScanConvMode = ADC_SCAN_DISABLE;
    adc_.Init.ContinuousConvMode = DISABLE;
    adc_.Init.DiscontinuousConvMode = DISABLE;
    adc_.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc_.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc_.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&adc_) != HAL_OK)
        return false;
    return HAL_ADCEx_Calibration_Start(&adc_) == HAL_OK;
}

bool ResistiveTouchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity, std::size_t& count)
{
    count = 0;
    if (points == nullptr || capacity == 0)
        return false;
    if (!layers_connected_())
    {
        if (was_pressed_)
        {
            points[0].state = brick::interfaces::display::TouchState::released;
            count = 1;
        }
        was_pressed_ = false;
        return true;
    }

    std::uint16_t raw_x = 0;
    std::uint16_t raw_y = 0;
    if (!sample_position_(raw_x, raw_y))
        return false;
    auto& point = points[0];
    point.id = 0;
    point.raw_x = raw_x;
    point.raw_y = raw_y;
    point.x = map_(raw_x, config_.raw_x_min, config_.raw_x_max, config_.width, config_.invert_x);
    point.y = map_(raw_y, config_.raw_y_min, config_.raw_y_max, config_.height, config_.invert_y);
    point.state = was_pressed_ ? brick::interfaces::display::TouchState::moved : brick::interfaces::display::TouchState::pressed;
    was_pressed_ = true;
    count = 1;
    return true;
}

bool ResistiveTouchscreen::layers_connected_()
{
    configure_pin_(GPIOC, kX1, GPIO_MODE_INPUT, GPIO_PULLUP);
    configure_pin_(GPIOB, kX2, GPIO_MODE_INPUT);
    configure_pin_(GPIOC, kY1, GPIO_MODE_OUTPUT_PP);
    configure_pin_(GPIOB, kY2, GPIO_MODE_OUTPUT_PP);
    GPIOC->BSRR = static_cast<std::uint32_t>(kY1) << 16;
    GPIOB->BSRR = static_cast<std::uint32_t>(kY2) << 16;
    std::uint16_t sample = 0;
    return sample_adc_(ADC_CHANNEL_14, sample) && sample < config_.pressed_threshold;
}

bool ResistiveTouchscreen::sample_position_(std::uint16_t& raw_x, std::uint16_t& raw_y)
{
    configure_pin_(GPIOC, kX1, GPIO_MODE_OUTPUT_PP);
    configure_pin_(GPIOB, kX2, GPIO_MODE_OUTPUT_PP);
    configure_pin_(GPIOC, kY1, GPIO_MODE_ANALOG);
    configure_pin_(GPIOB, kY2, GPIO_MODE_INPUT);
    GPIOC->BSRR = kX1;
    GPIOB->BSRR = static_cast<std::uint32_t>(kX2) << 16;
    if (!sample_adc_(ADC_CHANNEL_15, raw_x))
        return false;

    configure_pin_(GPIOC, kX1, GPIO_MODE_ANALOG);
    configure_pin_(GPIOB, kX2, GPIO_MODE_INPUT);
    configure_pin_(GPIOC, kY1, GPIO_MODE_OUTPUT_PP);
    configure_pin_(GPIOB, kY2, GPIO_MODE_OUTPUT_PP);
    GPIOC->BSRR = kY1;
    GPIOB->BSRR = static_cast<std::uint32_t>(kY2) << 16;
    return sample_adc_(ADC_CHANNEL_14, raw_y);
}

bool ResistiveTouchscreen::sample_adc_(std::uint32_t channel, std::uint16_t& value)
{
    ADC_ChannelConfTypeDef channel_config{};
    channel_config.Channel = channel;
    channel_config.Rank = ADC_REGULAR_RANK_1;
    channel_config.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    if (HAL_ADC_ConfigChannel(&adc_, &channel_config) != HAL_OK || HAL_ADC_Start(&adc_) != HAL_OK)
        return false;
    const bool ready = HAL_ADC_PollForConversion(&adc_, 10) == HAL_OK;
    if (ready)
        value = static_cast<std::uint16_t>(HAL_ADC_GetValue(&adc_));
    HAL_ADC_Stop(&adc_);
    return ready;
}

void ResistiveTouchscreen::configure_pin_(GPIO_TypeDef* port, std::uint16_t pin, std::uint32_t mode, std::uint32_t pull) const
{
    GPIO_InitTypeDef init{};
    init.Pin = pin;
    init.Mode = mode;
    init.Pull = pull;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &init);
}

std::int16_t ResistiveTouchscreen::map_(std::uint16_t value, std::uint16_t minimum, std::uint16_t maximum, std::uint16_t size, bool invert) const
{
    std::int32_t mapped = (static_cast<std::int32_t>(value) - minimum) * size / (maximum - minimum);
    if (mapped < 0)
        mapped = 0;
    if (mapped >= size)
        mapped = size - 1;
    return static_cast<std::int16_t>(invert ? size - 1 - mapped : mapped);
}

}  // namespace brick::platform::stm32::f1
