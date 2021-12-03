#include <zephyr.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(battery);

static void battery_entry_point(void *, void *, void *);

K_THREAD_DEFINE(sensor_battery, CONFIG_SUBSYS_BATTERY_STACK_SIZE,
                battery_entry_point, NULL, NULL, NULL,
                CONFIG_SUBSYS_BATTERY_THREAD_PRIORITY, 0, 0);

#define REGISTER_PUBLISHABLE_SENSOR_VALUE(name, max_count)               \
    static battery_value_cb name##_callbacks[max_count];                   \
    static int num_##name##_callbacks_registered = 0;                    \
    K_MUTEX_DEFINE(battery_##name##_callback_mutex);                       \
                                                                         \
    void battery_register_##name##_handler(battery_value_cb cb)              \
    {                                                                    \
        bool success = true;                                             \
                                                                         \
        k_mutex_lock(&battery_##name##_callback_mutex, K_FOREVER);         \
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
        k_mutex_unlock(&battery_##name##_callback_mutex);                  \
                                                                         \
        if (!success)                                                    \
        {                                                                \
            LOG_ERR("Unable to register " STRINGIFY(name) " callback!"); \
        }                                                                \
    }                                                                    \
                                                                         \
    static void publish_##name##_value(struct sensor_value value)        \
    {                                                                    \
        k_mutex_lock(&battery_##name##_callback_mutex, K_FOREVER);         \
        for (int i = 0; i < num_##name##_callbacks_registered; i++)      \
        {                                                                \
            name##_callbacks[i](value);                                  \
        }                                                                \
        k_mutex_unlock(&battery_##name##_callback_mutex);                  \
    }

REGISTER_PUBLISHABLE_SENSOR_VALUE(battery, CONFIG_SUBSYS_BATTERY_CALLBACK_MAX_COUNT);

static void battery_entry_point(void *u1, void *u2, void *u3)
{
    // Initialize BATTERY
    battery_init();

    while (1)
    {
        battery_start();
        k_sleep(K_MSEC(CONFIG_SUBSYS_BATTERY_SAMPLING_RATE_MS));
        int32_t voltage = battery_get_mv();
        publish_battery_value(voltage);
        }
    }
}
