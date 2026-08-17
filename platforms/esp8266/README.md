# BRICK ESP8266 Platform

ESP8266 adapters for small SPI displays and GPIO inputs. The ST7789 adapter
uses the TFT_eSPI library supplied by the application, while the GPIO button
adapter only depends on the Arduino GPIO API.

The first hardware profile targets the ESP-12F weather-station board:

- ST7789V, 240x240, SPI, CS tied to ground,
- display backlight on GPIO5, active low,
- TTP223/button output on GPIO4, active high.
