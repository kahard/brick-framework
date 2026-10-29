#pragma once

#include "brick/boards/esp32/p4/Jc1060BoardTemplate.h"

namespace brick::platform::esp32::p4
{

// Default board configuration. Applications that need a different set of
// peripherals should instantiate Jc1060BoardTemplate with their Features type.
using Jc1060Board = Jc1060BoardTemplate<>;

} // namespace brick::platform::esp32::p4
