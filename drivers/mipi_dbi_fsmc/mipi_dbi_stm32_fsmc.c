// SPDX-License-Identifier: Apache-2.0
#define DT_DRV_COMPAT st_stm32_fsmc_mipi_dbi

#include <soc.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include <drivers/shared_pins.h>

LOG_MODULE_REGISTER(mipi_dbi_fsmc, CONFIG_MIPI_DBI_LOG_LEVEL);

struct fsmc_config {
	uintptr_t registers;
	uintptr_t command;
	uintptr_t pixels;
	struct stm32_pclken clock;
	const struct pinctrl_dev_config *pinctrl;
	const struct device *pins;
	const struct device *dma;
	uint32_t channel;
	uint32_t slot;
	uint32_t fifo;
	uint32_t address_cycles;
	uint32_t data_cycles;
	struct gpio_dt_spec cs;
	struct gpio_dt_spec reset;
};

struct fsmc_data {
	struct k_sem complete;
	struct dma_config dma;
	struct dma_block_config block;
	int result;
	uint32_t clock_hz;
};

static void fsmc_dma_done(const struct device *dma, void *user_data, uint32_t channel, int status)
{
	struct fsmc_data *data = user_data;

	ARG_UNUSED(dma);
	ARG_UNUSED(channel);
	if (status == DMA_STATUS_BLOCK) {
		return;
	}
	data->result = status < 0 ? status : 0;
	k_sem_give(&data->complete);
}

static int fsmc_begin(const struct device *dev, const struct mipi_dbi_config *dbi)
{
	const struct fsmc_config *cfg = dev->config;
	struct fsmc_data *data = dev->data;
	int ret;

	if (dbi->mode != MIPI_DBI_MODE_8080_BUS_8_BIT) {
		return -ENOTSUP;
	}
	if ((uint64_t)dbi->config.frequency * (cfg->address_cycles + cfg->data_cycles) <
	    data->clock_hz) {
		return -EINVAL;
	}

	ret = shared_pins_acquire(cfg->pins, K_FOREVER);
	if (ret < 0) {
		return ret;
	}
	/* The keyboard leaves the data lines in GPIO mode after each scan. */
	ret = pinctrl_apply_state(cfg->pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret == 0) {
		ret = gpio_pin_set_dt(&cfg->cs, 1);
	}
	if (ret < 0) {
		shared_pins_release(cfg->pins);
	}
	return ret;
}

static int fsmc_end(const struct device *dev, int result)
{
	const struct fsmc_config *cfg = dev->config;
	int ret;

	barrier_dsync_fence_full();
	ret = gpio_pin_set_dt(&cfg->cs, 0);
	shared_pins_release(cfg->pins);
	return result < 0 ? result : ret;
}

static int fsmc_command_write(const struct device *dev, const struct mipi_dbi_config *dbi,
			      uint8_t command, const uint8_t *buf, size_t len)
{
	const struct fsmc_config *cfg = dev->config;
	int ret;

	if (len != 0 && buf == NULL) {
		return -EINVAL;
	}
	ret = fsmc_begin(dev, dbi);
	if (ret < 0) {
		return ret;
	}
	sys_write8(command, cfg->command);
	barrier_dsync_fence_full();
	for (size_t i = 0; i < len; ++i) {
		sys_write8(buf[i], cfg->pixels);
	}
	return fsmc_end(dev, 0);
}

static int fsmc_write_display(const struct device *dev, const struct mipi_dbi_config *dbi,
			      const uint8_t *buf, struct display_buffer_descriptor *desc,
			      enum display_pixel_format format)
{
	const struct fsmc_config *cfg = dev->config;
	struct fsmc_data *data = dev->data;
	size_t bytes;
	int ret;

	if (format != PIXEL_FORMAT_RGB_565X || desc == NULL || buf == NULL || desc->width == 0 ||
	    desc->height == 0 || desc->pitch != desc->width) {
		return -EINVAL;
	}
	if (desc->height > desc->buf_size / (2U * desc->width)) {
		return -EINVAL;
	}
	bytes = (size_t)desc->width * desc->height * 2U;
	ret = fsmc_begin(dev, dbi);
	if (ret < 0) {
		return ret;
	}

	while (bytes != 0) {
		size_t chunk = MIN(bytes, UINT16_MAX);

		k_sem_reset(&data->complete);
		data->result = -EIO;
		data->block.source_address = (uint32_t)(uintptr_t)buf;
		data->block.block_size = chunk;
		ret = dma_config(cfg->dma, cfg->channel, &data->dma);
		if (ret < 0) {
			break;
		}
		ret = dma_start(cfg->dma, cfg->channel);
		if (ret == 0) {
			ret = k_sem_take(&data->complete, K_MSEC(1000));
			ret = ret < 0 ? -ETIMEDOUT : data->result;
		}
		/* Stop before releasing the shared pins or returning the caller's buffer. */
		int stop_ret = dma_stop(cfg->dma, cfg->channel);

		if (ret == 0) {
			ret = stop_ret;
		}
		if (ret < 0) {
			break;
		}
		bytes -= chunk;
		buf += chunk;
	}
	if (ret < 0) {
		LOG_ERR("LCD DMA transfer failed: %d", ret);
	}
	return fsmc_end(dev, ret);
}

static int fsmc_reset(const struct device *dev, k_timeout_t delay)
{
	const struct fsmc_config *cfg = dev->config;
	int ret = shared_pins_acquire(cfg->pins, K_FOREVER);

	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_set_dt(&cfg->reset, 1);
	if (ret == 0) {
		k_sleep(delay);
		ret = gpio_pin_set_dt(&cfg->reset, 0);
	}
	shared_pins_release(cfg->pins);
	return ret;
}

static int fsmc_init(const struct device *dev)
{
	const struct fsmc_config *cfg = dev->config;
	struct fsmc_data *data = dev->data;
	const struct device *clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int ret;

	if (!device_is_ready(clock) || !device_is_ready(cfg->dma) || !device_is_ready(cfg->pins) ||
	    !gpio_is_ready_dt(&cfg->cs) || !gpio_is_ready_dt(&cfg->reset)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&cfg->cs, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}
	ret = clock_control_on(clock, (clock_control_subsys_t)&cfg->clock);
	if (ret < 0) {
		return ret;
	}
	ret = clock_control_get_rate(clock, (clock_control_subsys_t)&cfg->clock, &data->clock_hz);
	if (ret < 0) {
		return ret;
	}
	ret = pinctrl_apply_state(cfg->pinctrl, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/* Eight-bit SRAM mode maps RS directly to the selected CPU address bit. */
	sys_write32(0, cfg->registers);
	sys_write32(((cfg->address_cycles - 1U) << FSMC_BTR1_ADDSET_Pos) |
			    ((cfg->data_cycles - 1U) << FSMC_BTR1_DATAST_Pos),
		    cfg->registers + 4U);
	sys_write32(FSMC_BCR1_MBKEN | FSMC_BCR1_WREN, cfg->registers);
	barrier_dsync_fence_full();

	k_sem_init(&data->complete, 0, 1);
	data->block.dest_address = cfg->pixels;
	data->block.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	data->block.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	data->block.fifo_mode_control = cfg->fifo;
	data->dma.channel_direction = MEMORY_TO_MEMORY;
	data->dma.source_data_size = 1;
	data->dma.dest_data_size = 1;
	data->dma.source_burst_length = 1;
	data->dma.dest_burst_length = 1;
	data->dma.block_count = 1;
	data->dma.head_block = &data->block;
	data->dma.dma_slot = cfg->slot;
	data->dma.dma_callback = fsmc_dma_done;
	data->dma.user_data = data;
	return 0;
}

static DEVICE_API(mipi_dbi, fsmc_api) = {
	.command_write = fsmc_command_write,
	.write_display = fsmc_write_display,
	.reset = fsmc_reset,
};

#define FSMC_DEFINE(n)                                                                             \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	BUILD_ASSERT(DT_INST_PROP(n, address_setup_cycles) >= 1 &&                                 \
		     DT_INST_PROP(n, address_setup_cycles) <= 16);                                 \
	BUILD_ASSERT(DT_INST_PROP(n, data_setup_cycles) >= 1 &&                                    \
		     DT_INST_PROP(n, data_setup_cycles) <= 256);                                   \
	static const struct fsmc_config fsmc_config_##n = {                                        \
		.registers = DT_INST_REG_ADDR_BY_NAME(n, controller),                              \
		.command = DT_INST_REG_ADDR_BY_NAME(n, command),                                   \
		.pixels = DT_INST_REG_ADDR_BY_NAME(n, data),                                       \
		.clock = {.bus = DT_INST_CLOCKS_CELL(n, bus),                                      \
			  .enr = DT_INST_CLOCKS_CELL(n, bits)},                                    \
		.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                      \
		.pins = DEVICE_DT_GET(DT_INST_PHANDLE(n, shared_pins)),                            \
		.dma = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, tx)),                            \
		.channel = DT_INST_DMAS_CELL_BY_NAME(n, tx, channel),                              \
		.slot = DT_INST_DMAS_CELL_BY_NAME(n, tx, slot),                                    \
		.fifo = DT_INST_DMAS_CELL_BY_NAME(n, tx, features),                                \
		.address_cycles = DT_INST_PROP(n, address_setup_cycles),                           \
		.data_cycles = DT_INST_PROP(n, data_setup_cycles),                                 \
		.cs = GPIO_DT_SPEC_INST_GET(n, cs_gpios),                                          \
		.reset = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                                    \
	};                                                                                         \
	static struct fsmc_data fsmc_data_##n;                                                     \
	DEVICE_DT_INST_DEFINE(n, fsmc_init, NULL, &fsmc_data_##n, &fsmc_config_##n, POST_KERNEL,   \
			      CONFIG_MIPI_DBI_INIT_PRIORITY, &fsmc_api);

DT_INST_FOREACH_STATUS_OKAY(FSMC_DEFINE)
