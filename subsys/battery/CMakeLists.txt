
zephyr_library_named(subsys_battery)
zephyr_library_sources_ifdef(CONFIG_SUBSYS_BATTERY battery.c)