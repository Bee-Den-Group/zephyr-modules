# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_BATTERY battery.c)
zephyr_include_directories(.)