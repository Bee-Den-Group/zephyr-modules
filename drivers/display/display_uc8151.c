/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gooddisplay_uc8151

#include <string.h>
#include <device.h>
#include <init.h>
#include <drivers/display.h>
#include <drivers/gpio.h>
#include <drivers/spi.h>
#include <sys/byteorder.h>

#include "display_uc8151.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(uc8151, CONFIG_DISPLAY_LOG_LEVEL);

/**
 * UC8151 compatible EPD controller driver.
 *
 * Currently only the black/white pannels are supported (KW mode),
 * also first gate/source should be 0.
 */

#define UC8151_DC_PIN DT_INST_GPIO_PIN(0, dc_gpios)
#define UC8151_DC_FLAGS DT_INST_GPIO_FLAGS(0, dc_gpios)
#define UC8151_DC_CNTRL DT_INST_GPIO_LABEL(0, dc_gpios))
#define UC8151_BUSY_PIN DT_INST_GPIO_PIN(0, busy_gpios)
#define UC8151_BUSY_CNTRL DT_INST_GPIO_LABEL(0, busy_gpios)
#define UC8151_BUSY_FLAGS DT_INST_GPIO_FLAGS(0, busy_gpios)
#define UC8151_RESET_PIN DT_INST_GPIO_PIN(0, reset_gpios)
#define UC8151_RESET_CNTRL DT_INST_GPIO_LABEL(0, reset_gpios)
#define UC8151_RESET_FLAGS DT_INST_GPIO_FLAGS(0, reset_gpios)

#define EPD_PANEL_WIDTH			DT_INST_PROP(0, width)
#define EPD_PANEL_HEIGHT		DT_INST_PROP(0, height)
#define UC8151_PIXELS_PER_BYTE		8U

/* Horizontally aligned page! */
#define UC8151_NUMOF_PAGES		(EPD_PANEL_WIDTH / \
					 UC8151_PIXELS_PER_BYTE)

struct uc8151_data {
	const struct uc8151_config *config;
	const struct device *reset;
	const struct device *dc;
	const struct device *busy;
	const struct device *cs;
};

struct uc8151_config {
	struct spi_dt_spec bus;
};

static uint8_t uc8151_softstart[] = DT_INST_PROP(0, softstart);
static uint8_t uc8151_pwr[] = DT_INST_PROP(0, pwr);

/* Border and data polarity settings */
static uint8_t bdd_polarity;

static bool blanking_on = true;

static inline int uc8151_write_cmd(struct uc8151_data *driver,
				   uint8_t cmd, uint8_t *data, size_t len)
{
	struct spi_buf buf = {.buf = &cmd, .len = sizeof(cmd)};
	struct spi_buf_set buf_set = {.buffers = &buf, .count = 1};

	gpio_pin_set(driver->dc, UC8151_DC_PIN, 1);
	if (spi_write_dt(&driver->config->bus, &buf_set)) {
		return -EIO;
	}

	if (data != NULL) {
		buf.buf = data;
		buf.len = len;
		gpio_pin_set(driver->dc, UC8151_DC_PIN, 0);
		if (spi_write_dt(&driver->config->bus, &buf_set)) {
			return -EIO;
		}
	}

	return 0;
}

static inline void uc8151_busy_wait(struct uc8151_data *driver)
{
	int pin = gpio_pin_get(driver->busy, UC8151_BUSY_PIN);

	while (pin > 0) {
		__ASSERT(pin >= 0, "Failed to get pin level");
		LOG_DBG("wait %u", pin);
		k_sleep(K_MSEC(UC8151_BUSY_DELAY));
		pin = gpio_pin_get(driver->busy, UC8151_BUSY_PIN);
	}
}

static int uc8151_update_display(const struct device *dev)
{
	struct uc8151_data *driver = dev->data;

	LOG_DBG("Trigger update sequence");
	if (uc8151_write_cmd(driver, UC8151_CMD_DRF, NULL, 0)) {
		return -EIO;
	}

	k_sleep(K_MSEC(UC8151_BUSY_DELAY));

	return 0;
}

static int uc8151_blanking_off(const struct device *dev)
{
	struct uc8151_data *driver = dev->data;

	if (blanking_on) {
		/* Update EPD pannel in normal mode */
		uc8151_busy_wait(driver);
		if (uc8151_update_display(dev)) {
			return -EIO;
		}
	}

	blanking_on = false;

	return 0;
}

static int uc8151_blanking_on(const struct device *dev)
{
	blanking_on = true;

	return 0;
}

static int uc8151_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc,
			const void *buf)
{
	struct uc8151_data *driver = dev->data;
	uint16_t x_end_idx = x + desc->width - 1;
	uint16_t y_end_idx = y + desc->height - 1;
	uint8_t ptl[UC8151_PTL_REG_LENGTH] = {0};
	size_t buf_len;

	LOG_DBG("x %u, y %u, height %u, width %u, pitch %u",
		x, y, desc->height, desc->width, desc->pitch);

	buf_len = MIN(desc->buf_size,
		      desc->height * desc->width / UC8151_PIXELS_PER_BYTE);
	__ASSERT(desc->width <= desc->pitch, "Pitch is smaller then width");
	__ASSERT(buf != NULL, "Buffer is not available");
	__ASSERT(buf_len != 0U, "Buffer of length zero");
	__ASSERT(!(desc->width % UC8151_PIXELS_PER_BYTE),
		 "Buffer width not multiple of %d", UC8151_PIXELS_PER_BYTE);

	if ((y_end_idx > (EPD_PANEL_HEIGHT - 1)) ||
	    (x_end_idx > (EPD_PANEL_WIDTH - 1))) {
		LOG_ERR("Position out of bounds");
		return -EINVAL;
	}

	/* Setup Partial Window and enable Partial Mode */
	sys_put_be16(x, &ptl[UC8151_PTL_HRST_IDX]);
	sys_put_be16(x_end_idx, &ptl[UC8151_PTL_HRED_IDX]);
	sys_put_be16(y, &ptl[UC8151_PTL_VRST_IDX]);
	sys_put_be16(y_end_idx, &ptl[UC8151_PTL_VRED_IDX]);
	ptl[sizeof(ptl) - 1] = UC8151_PTL_PT_SCAN;
	LOG_HEXDUMP_DBG(ptl, sizeof(ptl), "ptl");

	uc8151_busy_wait(driver);
	if (uc8151_write_cmd(driver, UC8151_CMD_PTIN, NULL, 0)) {
		return -EIO;
	}

	if (uc8151_write_cmd(driver, UC8151_CMD_PTL, ptl, sizeof(ptl))) {
		return -EIO;
	}

	/* Disable boarder output */
	bdd_polarity |= UC8151_CDI_BDZ;
	if (uc8151_write_cmd(driver, UC8151_CMD_CDI,
			     &bdd_polarity, sizeof(bdd_polarity))) {
		return -EIO;
	}

	if (uc8151_write_cmd(driver, UC8151_CMD_DTM2, (uint8_t *)buf, buf_len)) {
		return -EIO;
	}

	/* Update partial window and disable Partial Mode */
	if (blanking_on == false) {
		if (uc8151_update_display(dev)) {
			return -EIO;
		}
	}

	/* Enable boarder output */
	bdd_polarity &= ~UC8151_CDI_BDZ;
	if (uc8151_write_cmd(driver, UC8151_CMD_CDI,
			     &bdd_polarity, sizeof(bdd_polarity))) {
		return -EIO;
	}

	if (uc8151_write_cmd(driver, UC8151_CMD_PTOUT, NULL, 0)) {
		return -EIO;
	}

	return 0;
}

static int uc8151_read(const struct device *dev, const uint16_t x, const uint16_t y,
		       const struct display_buffer_descriptor *desc, void *buf)
{
	LOG_ERR("not supported");
	return -ENOTSUP;
}

static void *uc8151_get_framebuffer(const struct device *dev)
{
	LOG_ERR("not supported");
	return NULL;
}

static int uc8151_set_brightness(const struct device *dev,
				 const uint8_t brightness)
{
	LOG_WRN("not supported");
	return -ENOTSUP;
}

static int uc8151_set_contrast(const struct device *dev, uint8_t contrast)
{
	LOG_WRN("not supported");
	return -ENOTSUP;
}

static void uc8151_get_capabilities(const struct device *dev,
				    struct display_capabilities *caps)
{
	memset(caps, 0, sizeof(struct display_capabilities));
	caps->x_resolution = EPD_PANEL_WIDTH;
	caps->y_resolution = EPD_PANEL_HEIGHT;
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO10;
	caps->current_pixel_format = PIXEL_FORMAT_MONO10;
	caps->screen_info = SCREEN_INFO_MONO_MSB_FIRST | SCREEN_INFO_EPD;
}

static int uc8151_set_orientation(const struct device *dev,
				  const enum display_orientation
				  orientation)
{
	LOG_ERR("Unsupported");
	return -ENOTSUP;
}

static int uc8151_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pf)
{
	if (pf == PIXEL_FORMAT_MONO10) {
		return 0;
	}

	LOG_ERR("not supported");
	return -ENOTSUP;
}

static int uc8151_clear_and_write_buffer(const struct device *dev,
					 uint8_t pattern, bool update)
{
	struct display_buffer_descriptor desc = {
		.buf_size = UC8151_NUMOF_PAGES,
		.width = EPD_PANEL_WIDTH,
		.height = 1,
		.pitch = EPD_PANEL_WIDTH,
	};
	uint8_t *line;

	line = k_malloc(UC8151_NUMOF_PAGES);
	if (line == NULL) {
		return -ENOMEM;
	}

	memset(line, pattern, UC8151_NUMOF_PAGES);
	for (int i = 0; i < EPD_PANEL_HEIGHT; i++) {
		uc8151_write(dev, 0, i, &desc, line);
	}

	k_free(line);

	if (update == true) {
		if (uc8151_update_display(dev)) {
			return -EIO;
		}
	}

	return 0;
}

static int uc8151_controller_init(const struct device *dev)
{
	struct uc8151_data *driver = dev->data;
	uint8_t tmp[UC8151_TRES_REG_LENGTH];
	
	gpio_pin_set(driver->reset, UC8151_RESET_PIN, 1);
	k_sleep(K_MSEC(UC8151_RESET_DELAY));
	gpio_pin_set(driver->reset, UC8151_RESET_PIN, 0);
	k_sleep(K_MSEC(UC8151_RESET_DELAY));

	uc8151_busy_wait(driver);

	LOG_DBG("Initialize UC8151 controller");

	if (uc8151_write_cmd(driver, UC8151_CMD_PWR, uc8151_pwr,
			     sizeof(uc8151_pwr))) {
		return -EIO;
	}

	if (uc8151_write_cmd(driver, UC8151_CMD_BTST,
			     uc8151_softstart, sizeof(uc8151_softstart))) {
		return -EIO;
	}

	/* Turn on: booster, controller, regulators, and sensor. */
	if (uc8151_write_cmd(driver, UC8151_CMD_PON, NULL, 0)) {
		return -EIO;
	}

	k_sleep(K_MSEC(UC8151_PON_DELAY));
	uc8151_busy_wait(driver);

	/* Pannel settings, KW mode */
	tmp[0] = UC8151_PSR_KW_R |
		 UC8151_PSR_UD |
		 UC8151_PSR_SHL |
		 UC8151_PSR_SHD |
		 UC8151_PSR_RST;
	if (uc8151_write_cmd(driver, UC8151_CMD_PSR, tmp, 1)) {
		return -EIO;
	}

	/* Set panel resolution */
	sys_put_be16(EPD_PANEL_WIDTH, &tmp[UC8151_TRES_HRES_IDX]);
	sys_put_be16(EPD_PANEL_HEIGHT, &tmp[UC8151_TRES_VRES_IDX]);
	LOG_HEXDUMP_DBG(tmp, sizeof(tmp), "TRES");
	if (uc8151_write_cmd(driver, UC8151_CMD_TRES,
			     tmp, UC8151_TRES_REG_LENGTH)) {
		return -EIO;
	}

	bdd_polarity = UC8151_CDI_BDV1 |
		       UC8151_CDI_N2OCP | UC8151_CDI_DDX0;
	tmp[UC8151_CDI_BDZ_DDX_IDX] = bdd_polarity;
	tmp[UC8151_CDI_CDI_IDX] = DT_INST_PROP(0, cdi);
	LOG_HEXDUMP_DBG(tmp, UC8151_CDI_REG_LENGTH, "CDI");
	if (uc8151_write_cmd(driver, UC8151_CMD_CDI, tmp,
			     UC8151_CDI_REG_LENGTH)) {
		return -EIO;
	}

	tmp[0] = DT_INST_PROP(0, tcon);
	if (uc8151_write_cmd(driver, UC8151_CMD_TCON, tmp, 1)) {
		return -EIO;
	}

	/* Enable Auto Sequence */
	tmp[0] = UC8151_AUTO_PON_DRF_POF;
	if (uc8151_write_cmd(driver, UC8151_CMD_AUTO, tmp, 1)) {
		return -EIO;
	}

	if (uc8151_clear_and_write_buffer(dev, 0xff, false)) {
		return -1;
	}

	return 0;
}

static int uc8151_init(const struct device *dev)
{
	const struct uc8151_config *config = dev->config;
	struct uc8151_data *driver = dev->data;

	LOG_DBG("");

	if (!spi_is_ready(&config->bus)) {
		LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	driver->reset = device_get_binding(UC8151_RESET_CNTRL);
	if (driver->reset == NULL) {
		LOG_ERR("Could not get GPIO port for UC8151 reset");
		return -EIO;
	}

	gpio_pin_configure(driver->reset, UC8151_RESET_PIN,
			   GPIO_OUTPUT_INACTIVE | UC8151_RESET_FLAGS);

	driver->dc = device_get_binding(UC8151_DC_CNTRL);
	if (driver->dc == NULL) {
		LOG_ERR("Could not get GPIO port for UC8151 DC signal");
		return -EIO;
	}

	gpio_pin_configure(driver->dc, UC8151_DC_PIN,
			   GPIO_OUTPUT_INACTIVE | UC8151_DC_FLAGS);

	driver->busy = device_get_binding(UC8151_BUSY_CNTRL);
	if (driver->busy == NULL) {
		LOG_ERR("Could not get GPIO port for UC8151 busy signal");
		return -EIO;
	}

	gpio_pin_configure(driver->busy, UC8151_BUSY_PIN,
			   GPIO_INPUT | UC8151_BUSY_FLAGS);

	return uc8151_controller_init(dev);
}

static struct uc8151_data uc8151_driver = {
	.config = &uc8151_config
};

static const struct uc8151_config uc8151_config {
	.bus = SPI_DT_SPEC_INST_GET(
		0, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0)
};

static struct display_driver_api uc8151_driver_api = {
	.blanking_on = uc8151_blanking_on,
	.blanking_off = uc8151_blanking_off,
	.write = uc8151_write,
	.read = uc8151_read,
	.get_framebuffer = uc8151_get_framebuffer,
	.set_brightness = uc8151_set_brightness,
	.set_contrast = uc8151_set_contrast,
	.get_capabilities = uc8151_get_capabilities,
	.set_pixel_format = uc8151_set_pixel_format,
	.set_orientation = uc8151_set_orientation,
};


DEVICE_DT_INST_DEFINE(0, uc8151_init, NULL,
		      &uc8151_driver, &uc8151_config,
		      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,
		      &uc8151_driver_api);