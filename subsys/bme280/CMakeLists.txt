
zephyr_library_named(subsys_bme280)
zephyr_library_sources_ifdef(CONFIG_SUBSYS_BME280 bme280.c)
zephyr_include_directories(.)
