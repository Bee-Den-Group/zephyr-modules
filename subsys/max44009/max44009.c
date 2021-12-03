#include "max44009.h"

#include <zephyr.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(max44009);

static void max44009_entry_point(void *, void *, void *);

K_THREAD_DEFINE(sensor_max44009, CONFIG_SUBSYS_MAX44009_STACK_SIZE,
                max44009_entry_point, NULL, NULL, NULL,
                CONFIG_SUBSYS_MAX44009_THREAD_PRIORITY, 0, 0);

#define REGISTER_PUBLISHABLE_SENSOR_VALUE(name, max_count)               \
    static max44009_value_cb name##_callbacks[max_count];                   \
    static int num_##name##_callbacks_registered = 0;                    \
    K_MUTEX_DEFINE(max44009_##name##_callback_mutex);                       \
                                                                         \
    void max44009_register_##name##_handler(max44009_value_cb cb)              \
    {                                                                    \
        bool success = true;                                             \
                                                                         \
        k_mutex_lock(&max44009_##name##_callback_mutex, K_FOREVER);         \
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
        k_mutex_unlock(&max44009_##name##_callback_mutex);                  \
                                                                         \
        if (!success)                                                    \
        {                                                                \
            LOG_ERR("Unable to register " STRINGIFY(name) " callback!"); \
        }                                                                \
    }                                                                    \
                                                                         \
    static void publish_##name##_value(struct sensor_value value)        \
    {                                                                    \
        k_mutex_lock(&max44009_##name##_callback_mutex, K_FOREVER);         \
        for (int i = 0; i < num_##name##_callbacks_registered; i++)      \
        {                                                                \
            name##_callbacks[i](value);                                  \
        }                                                                \
        k_mutex_unlock(&max44009_##name##_callback_mutex);                  \
    }

REGISTER_PUBLISHABLE_SENSOR_VALUE(luminosity, CONFIG_SUBSYS_MAX44009_CALLBACK_MAX_COUNT_AMBIENT_LIGHT);

int max44009_fail_counter = 0;

static void max44009_entry_point(void *u1, void *u2, void *u3)
{
    // Initialize MAX44009 luminosity sensor
    const struct device *max44009 = device_get_binding("MAX44009");
    if (!max44009)
    {
        LOG_ERR("Failed to find sensor %s!", "MAX44009");
        return;
    }

    struct sensor_value luminosity;

    while (1)
    {
        k_sleep(K_MSEC(CONFIG_SUBSYS_MAX44009_SAMPLING_RATE_MS));

        int success;

        success = sensor_sample_fetch(max44009);

        if (success != 0)
        {
            LOG_WRN("Sensor fetch failed: %d", success);

            // If we fail too many times in a row, publish that we are
            // now in an error state.
            // Don't publish it right away, because sporadic fails seem to happen
            // regularly.
            max44009_fail_counter += 1;
            if (max44009_fail_counter >= CONFIG_SUBSYS_MAX44009_MAX_FETCH_ATTEMPTS)
            {
                publish_luminosity_value(MAX44009_ERROR_VALUE);
            }

            continue;
        }
        max44009_fail_counter = 0;
        
        success = sensor_sample_fetch_chan(max44009,SENSOR_CHAN_LIGHT);
        if (success != 0)
        {
            LOG_WRN("get failed: %d", success);
            publish_luminosity_value(MAX44009_ERROR_VALUE);
        }

        success = sensor_channel_get(max44009, SENSOR_CHAN_LIGHT,
                                     &luminosity);
        if (success != 0)
        {
            LOG_WRN("get failed: %d", success);
            publish_luminosity_value(MAX44009_ERROR_VALUE);
        }
        else
        {
            publish_luminosity_value(luminosity);
        }

    }
}
