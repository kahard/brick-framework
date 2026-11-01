#include "brick/platform/stm32/f1/ResistiveTouchscreen.h"

#include "brick/platform/stm32/f1/HalStatus.h"

namespace brick::platform::stm32::f1
{

namespace
{

    constexpr std::uint32_t kConversionTimeoutMs = 10;

}  // namespace

Status ResistiveTouchscreen::begin()
{
    if (started_)
        return Status::ok;
    if (config_.adc == nullptr || !config_.x_plus.connected() || !config_.x_minus.connected()
        || !config_.y_plus.connected() || !config_.y_minus.connected() || config_.display_size.width == 0
        || config_.display_size.height == 0)
        return Status::invalid_argument;
    if (const Status status = enable_clock(config_.adc); !is_ok(status))
        return status;
    // PCLK2 / 6 keeps the ADC clock within its 14 MHz limit at 72 MHz.
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);
    adc_.Instance                   = config_.adc;
    adc_.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    adc_.Init.ContinuousConvMode    = DISABLE;
    adc_.Init.DiscontinuousConvMode = DISABLE;
    adc_.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    adc_.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    adc_.Init.NbrOfConversion       = 1;
    if (const HAL_StatusTypeDef result = HAL_ADC_Init(&adc_); result != HAL_OK)
        return to_status(result);
    const Status status = to_status(HAL_ADCEx_Calibration_Start(&adc_));
    started_            = is_ok(status);
    return status;
}

Status ResistiveTouchscreen::read(brick::interfaces::display::TouchPoint* points, std::size_t capacity,
                                  std::size_t& count)
{
    using brick::interfaces::display::TouchState;
    count = 0;
    if (!started_)
        return Status::not_ready;
    if (points == nullptr || capacity == 0)
        return Status::invalid_argument;
    if (!layers_touch_())
    {
        if (!was_pressed_)
            return Status::no_data;
        points[0].state = TouchState::released;
        count           = 1;
        was_pressed_    = false;
        return Status::ok;
    }
    std::uint16_t raw_x = 0;
    std::uint16_t raw_y = 0;
    if (const Status status = sample_position_(raw_x, raw_y); !is_ok(status))
        return status;
    points[0]       = mapper_.map(0, static_cast<std::int16_t>(raw_x), static_cast<std::int16_t>(raw_y));
    points[0].state = was_pressed_ ? TouchState::moved : TouchState::pressed;
    was_pressed_    = true;
    count           = 1;
    return Status::ok;
}

bool ResistiveTouchscreen::layers_touch_()
{
    // X+ pulled up, Y plate grounded: X+ falls when the layers touch.
    configure(config_.x_plus, GPIO_MODE_INPUT, GPIO_PULLUP);
    configure(config_.x_minus, GPIO_MODE_INPUT);
    configure(config_.y_plus, GPIO_MODE_OUTPUT_PP);
    configure(config_.y_minus, GPIO_MODE_OUTPUT_PP);
    write(config_.y_plus, false);
    write(config_.y_minus, false);
    std::uint16_t sample = 0;
    return is_ok(sample_adc_(config_.x_plus_channel, sample)) && sample < config_.pressed_threshold;
}

Status ResistiveTouchscreen::sample_position_(std::uint16_t& raw_x, std::uint16_t& raw_y)
{
    // Voltage across the X plate, read on Y+.
    configure(config_.x_plus, GPIO_MODE_OUTPUT_PP);
    configure(config_.x_minus, GPIO_MODE_OUTPUT_PP);
    configure(config_.y_plus, GPIO_MODE_ANALOG);
    configure(config_.y_minus, GPIO_MODE_INPUT);
    write(config_.x_plus, true);
    write(config_.x_minus, false);
    if (const Status status = sample_adc_(config_.y_plus_channel, raw_x); !is_ok(status))
        return status;
    // Voltage across the Y plate, read on X+.
    configure(config_.x_plus, GPIO_MODE_ANALOG);
    configure(config_.x_minus, GPIO_MODE_INPUT);
    configure(config_.y_plus, GPIO_MODE_OUTPUT_PP);
    configure(config_.y_minus, GPIO_MODE_OUTPUT_PP);
    write(config_.y_plus, true);
    write(config_.y_minus, false);
    return sample_adc_(config_.x_plus_channel, raw_y);
}

Status ResistiveTouchscreen::sample_adc_(std::uint32_t channel, std::uint16_t& value)
{
    ADC_ChannelConfTypeDef channel_config{};
    channel_config.Channel      = channel;
    channel_config.Rank         = ADC_REGULAR_RANK_1;
    channel_config.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    if (const HAL_StatusTypeDef result = HAL_ADC_ConfigChannel(&adc_, &channel_config); result != HAL_OK)
        return to_status(result);
    if (const HAL_StatusTypeDef result = HAL_ADC_Start(&adc_); result != HAL_OK)
        return to_status(result);
    const HAL_StatusTypeDef conversion = HAL_ADC_PollForConversion(&adc_, kConversionTimeoutMs);
    if (conversion == HAL_OK)
        value = static_cast<std::uint16_t>(HAL_ADC_GetValue(&adc_));
    HAL_ADC_Stop(&adc_);
    return to_status(conversion);
}

}  // namespace brick::platform::stm32::f1
