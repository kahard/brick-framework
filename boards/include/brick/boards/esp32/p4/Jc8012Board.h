#pragma once

#include "brick/boards/esp32/p4/Jc8012BoardTemplate.h"

namespace brick::platform::esp32::p4
{

// Default board configuration. Applications that need a different set of
// peripherals should instantiate Jc8012BoardTemplate with their Features type.
using Jc8012Board = Jc8012BoardTemplate<>;

} // namespace brick::platform::esp32::p4
