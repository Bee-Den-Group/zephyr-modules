#pragma once

#include <drivers/sensor.h>

typedef void (*bme280_value_cb)(struct sensor_value value);

void bme280_register_temperature_handler(bme280_value_cb cb);
void bme280_register_humidity_handler(bme280_value_cb cb);
void bme280_register_pressure_handler(bme280_value_cb cb);

static const struct sensor_value BME280_ERROR_VALUE = {.val1 = 0, .val2 = -1};
