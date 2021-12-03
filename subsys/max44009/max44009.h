#pragma once

#include <drivers/sensor.h>

typedef void (*max44009_value_cb)(struct sensor_value value);

void max44009_register_luminosity_handler(max44009_value_cb cb);

static const struct sensor_value MAX44009_ERROR_VALUE = {.val1 = 0, .val2 = -1};
