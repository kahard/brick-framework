#pragma once

#include "brick/boards/esp32/s3/Panel480BoardTemplate.h"

namespace brick::platform::esp32::s3
{

// Default board configuration. Applications that need a different set of
// peripherals should instantiate Panel480BoardTemplate with their Features type.
using Panel480Board = Panel480BoardTemplate<>;

} // namespace brick::platform::esp32::s3
