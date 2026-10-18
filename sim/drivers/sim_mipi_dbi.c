// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/* MIPI DBI controller that feeds the HX8353E model at API level, with no GPIO bus. */

#define DT_DRV_COMPAT dmrtastic_sim_mipi_dbi

#include <zephyr/device.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/kernel.h>

#include <sim/sim_lcd.h>

static int sim_dbi_command_write(const struct device *dev, const struct mipi_dbi_config *config,
				 uint8_t cmd, const uint8_t *data, size_t len)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(config);

	sim_lcd_cmd(cmd);
	for (size_t i = 0; i < len; i++) {
		sim_lcd_data(data[i]);
	}
	return 0;
}

static int sim_dbi_write_display(const struct device *dev, const struct mipi_dbi_config *config,
				 const uint8_t *framebuf, struct display_buffer_descriptor *desc,
				 enum display_pixel_format pixfmt)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(config);
	ARG_UNUSED(pixfmt);

	for (size_t i = 0; i < desc->buf_size; i++) {
		sim_lcd_data(framebuf[i]);
	}
	return 0;
}

static int sim_dbi_reset(const struct device *dev, k_timeout_t delay)
{
	ARG_UNUSED(dev);

	sim_lcd_hw_reset(true);
	k_sleep(delay);
	sim_lcd_hw_reset(false);
	return 0;
}

static DEVICE_API(mipi_dbi, sim_dbi_api) = {
	.command_write = sim_dbi_command_write,
	.write_display = sim_dbi_write_display,
	.reset = sim_dbi_reset,
};

#define SIM_DBI_INIT(n)                                                                            \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                              \
			      CONFIG_MIPI_DBI_INIT_PRIORITY, &sim_dbi_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_DBI_INIT)
