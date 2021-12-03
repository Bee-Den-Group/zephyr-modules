#include "bme280.h"

#include <zephyr.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(bme280);

static void bme280_entry_point(void *, void *, void *);

K_THREAD_DEFINE(sensor_bme280, CONFIG_SUBSYS_BME280_STACK_SIZE,
                bme280_entry_point, NULL, NULL, NULL,
                CONFIG_SUBSYS_BME280_THREAD_PRIORITY, 0, 0);

#define REGISTER_PUBLISHABLE_SENSOR_VALUE(name, max_count)               \
    static bme280_value_cb name##_callbacks[max_count];                   \
    static int num_##name##_callbacks_registered = 0;                    \
    K_MUTEX_DEFINE(bme280_##name##_callback_mutex);                       \
                                                                         \
    void bme280_register_##name##_handler(bme280_value_cb cb)              \
    {                                                                    \
        bool success = true;                                             \
                                                                         \
        k_mutex_lock(&bme280_##name##_callback_mutex, K_FOREVER);         \
                                                                         \
        if (num_##name##_callbacks_registered < max_count)               \
        {                                                                \
            name##_callbacks[num_##name##_callbacks_registered] = cb;    \
            num_##name##_callbacks_registered++;                         \
        }                                                                \
        else                                                             \
        {                                                                \
            success = false;                                             \
        }                                                                \
                                                                         \
        k_mutex_unlock(&bme280_##name##_callback_mutex);                  \
                                                                         \
        if (!success)                                                    \
        {                                                                \
            LOG_ERR("Unable to register " STRINGIFY(name) " callback!"); \
        }                                                                \
    }                                                                    \
                                                                         \
    static void publish_##name##_value(struct sensor_value value)        \
    {                                                                    \
        k_mutex_lock(&bme280_##name##_callback_mutex, K_FOREVER);         \
        for (int i = 0; i < num_##name##_callbacks_registered; i++)      \
        {                                                                \
            name##_callbacks[i](value);                                  \
        }                                                                \
        k_mutex_unlock(&bme280_##name##_callback_mutex);                  \
    }

REGISTER_PUBLISHABLE_SENSOR_VALUE(temperature, CONFIG_SUBSYS_BME280_CALLBACK_MAX_COUNT_TEMPERATURE);
REGISTER_PUBLISHABLE_SENSOR_VALUE(humidity, CONFIG_SUBSYS_BME280_CALLBACK_MAX_COUNT_HUMIDITY);
REGISTER_PUBLISHABLE_SENSOR_VALUE(pressure, CONFIG_SUBSYS_BME280_CALLBACK_MAX_COUNT_PRESSURE);

int bme280_fail_counter = 0;

static void bme280_entry_point(void *u1, void *u2, void *u3)
{
    // Initialize BME280 Temp+Humidity sensor
    const struct device *bme280 = device_get_binding("BME280");
    if (!bme280)
    {
        LOG_ERR("Failed to find sensor %s!", "BME280");
        return;
    }

    struct sensor_value temperature;
    struct sensor_value humidity;
    struct sensor_value pressure;

    while (1)
    {
        k_sleep(K_MSEC(CONFIG_SUBSYS_BME280_SAMPLING_RATE_MS));

        int success;

        success = sensor_sample_fetch(bme280);

        if (success != 0)
        {
            LOG_WRN("Sensor fetch failed: %d", success);

            // If we fail too many times in a row, publish that we are
            // now in an error state.
            // Don't publish it right away, because sporadic fails seem to happen
            // regularly.
            bme280_fail_counter += 1;
            if (bme280_fail_counter >= CONFIG_SUBSYS_BME280_MAX_FETCH_ATTEMPTS)
            {
                publish_temperature_value(BME280_ERROR_VALUE);
                publish_humidity_value(BME280_ERROR_VALUE);
                publish_pressure_value(BME280_ERROR_VALUE);
            }

            continue;
        }
        bme280_fail_counter = 0;

        success = sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP,
                                     &temperature);
        if (success != 0)
        {
            LOG_WRN("get failed: %d", success);
            publish_temperature_value(BME280_ERROR_VALUE);
        }
        else
        {
            publish_temperature_value(temperature);
        }

        success = sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY,
                                     &humidity);
        if (success != 0)
        {
            LOG_WRN("get failed: %d", success);
            publish_humidity_value(BME280_ERROR_VALUE);
        }
        else
        {
            publish_humidity_value(humidity);
        }

        success = sensor_channel_get(bme280, SENSOR_CHAN_PRESS,
                                     &pressure);
        if (success != 0)
        {
            LOG_WRN("get failed: %d", success);
            publish_pressure_value(BME280_ERROR_VALUE);
        }
        else
        {
            publish_pressure_value(pressure);
        }
    }
}
