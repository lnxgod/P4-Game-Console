# Waveshare battery telemetry

This component owns the bounded battery-voltage math and the ESP-IDF ADC
oneshot/calibration backend for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3.
The [official Waveshare schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4.3/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf) labels the sense net `BAT_ADC`, routes it to GPIO20, and shows 200 kΩ from `BAT` to the sense node plus 100 kΩ from the node to ground. The conversion is therefore `battery_mv = adc_mv * 3`.

The [ESP-IDF ADC oneshot API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/adc_oneshot.html) and the pinned ESP-IDF 5.5.3 ESP32-P4 ADC map identify GPIO20 as ADC1 channel 4 (ADC1 is GPIO16–23; ADC2 is GPIO49–54). Initialization checks that exact GPIO-to-channel/unit mapping and returns `ESP_ERR_NOT_SUPPORTED` rather than reading another pin if it changes.

The percent result is deliberately a bounded linear estimate between configurable
empty/full voltages (defaults 3300/4200 mV); it is not a fuel-gauge model.

No hardware was accessed or flashed by this component change.
