// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/*
 * IRQ-safe GPIO SPI bit-bang bus controller: like zephyr,spi-bitbang but
 * spinlock-protected (not spi_context's k_sem) so it works from ISR context.
 * Master-only, 8-bit words; SPI_HOLD_ON_CS/SPI_LOCK_ON unsupported.
 */

#define DT_DRV_COMPAT ht_radio_spi_bitbang_irq

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(spi_bitbang_irq, CONFIG_SPI_BITBANG_IRQ_LOG_LEVEL);

/* Max chip-select GPIOs configured at init; grow if a bus needs more. */
#define SPI_BITBANG_IRQ_MAX_CS 4

struct spi_bitbang_irq_config {
	struct gpio_dt_spec clk_gpio;
	struct gpio_dt_spec mosi_gpio;
	struct gpio_dt_spec miso_gpio;
	struct gpio_dt_spec cs_gpios[SPI_BITBANG_IRQ_MAX_CS];
	uint8_t cs_count;
};

struct spi_bitbang_irq_data {
	struct k_spinlock bus_lock;
};

/** Clock out/in one byte bit-by-bit per the given CPOL/CPHA/bit-order. */
static int spi_bitbang_irq_xfer_byte(const struct spi_bitbang_irq_config *cfg, uint8_t out,
				     bool cpol, bool cpha, bool lsb_first, bool have_miso,
				     uint8_t *in)
{
	uint8_t r = 0;
	const int idle = cpol ? 1 : 0;
	const int active = cpol ? 0 : 1;

	for (int i = 0; i < 8; i++) {
		const int bit = lsb_first ? i : (7 - i);
		const int d = (out >> bit) & 0x1;

		if (cfg->mosi_gpio.port != NULL) {
			gpio_pin_set_dt(&cfg->mosi_gpio, d);
		}

		/*
		 * CPHA=0: sample on leading edge, shift on trailing.
		 * CPHA=1: shift on leading edge, sample on trailing.
		 */
		if (!cpha && have_miso) {
			r |= (gpio_pin_get_dt(&cfg->miso_gpio) ? 1 : 0) << bit;
		}

		gpio_pin_set_dt(&cfg->clk_gpio, active);

		if (cpha && have_miso) {
			r |= (gpio_pin_get_dt(&cfg->miso_gpio) ? 1 : 0) << bit;
		}

		gpio_pin_set_dt(&cfg->clk_gpio, idle);
	}

	if (in != NULL) {
		*in = r;
	}
	return 0;
}

/** SPI transceive under a spinlock (not spi_context), so callable from ISR context. */
static int spi_bitbang_irq_transceive(const struct device *dev, const struct spi_config *spi_cfg,
				      const struct spi_buf_set *tx_bufs,
				      const struct spi_buf_set *rx_bufs)
{
	const struct spi_bitbang_irq_config *cfg = dev->config;
	struct spi_bitbang_irq_data *data = dev->data;

	if (spi_cfg->operation & SPI_OP_MODE_SLAVE) {
		return -ENOTSUP;
	}
	if (spi_cfg->operation & (SPI_LINES_DUAL | SPI_LINES_QUAD | SPI_LINES_OCTAL)) {
		return -ENOTSUP;
	}
	if (SPI_WORD_SIZE_GET(spi_cfg->operation) != 8) {
		return -ENOTSUP;
	}

	const bool cpol = (spi_cfg->operation & SPI_MODE_CPOL) != 0;
	const bool cpha = (spi_cfg->operation & SPI_MODE_CPHA) != 0;
	const bool lsb = (spi_cfg->operation & SPI_TRANSFER_LSB) != 0;
	const bool have_miso = cfg->miso_gpio.port != NULL;

	const struct spi_buf *tx = tx_bufs ? tx_bufs->buffers : NULL;
	const struct spi_buf *rx = rx_bufs ? rx_bufs->buffers : NULL;
	size_t tx_count = tx_bufs ? tx_bufs->count : 0;
	size_t rx_count = rx_bufs ? rx_bufs->count : 0;

	k_spinlock_key_t key = k_spin_lock(&data->bus_lock);

	gpio_pin_set_dt(&cfg->clk_gpio, cpol ? 1 : 0);

	const struct gpio_dt_spec *cs = spi_cfg->cs.gpio.port != NULL ? &spi_cfg->cs.gpio : NULL;

	if (cs != NULL) {
		gpio_pin_set_dt(cs, 1);
	}

	size_t tx_off = 0, rx_off = 0;
	while (tx_count > 0 || rx_count > 0) {
		uint8_t out = 0;
		uint8_t in = 0;

		if (tx_count > 0 && tx_off < tx->len && tx->buf != NULL) {
			out = ((const uint8_t *)tx->buf)[tx_off];
		}

		spi_bitbang_irq_xfer_byte(cfg, out, cpol, cpha, lsb, have_miso, &in);

		if (rx_count > 0 && rx_off < rx->len && rx->buf != NULL) {
			((uint8_t *)rx->buf)[rx_off] = in;
		}

		if (tx_count > 0) {
			tx_off++;
			if (tx_off >= tx->len) {
				tx++;
				tx_count--;
				tx_off = 0;
			}
		}
		if (rx_count > 0) {
			rx_off++;
			if (rx_off >= rx->len) {
				rx++;
				rx_count--;
				rx_off = 0;
			}
		}
	}

	if (cs != NULL) {
		gpio_pin_set_dt(cs, 0);
	}

	k_spin_unlock(&data->bus_lock, key);
	return 0;
}

static int spi_bitbang_irq_release(const struct device *dev, const struct spi_config *spi_cfg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(spi_cfg);
	return 0;
}

static DEVICE_API(spi, spi_bitbang_irq_api) = {
	.transceive = spi_bitbang_irq_transceive,
	.release = spi_bitbang_irq_release,
};

/** Configure clk/mosi/miso and all declared CS lines as GPIOs. */
static int spi_bitbang_irq_init(const struct device *dev)
{
	const struct spi_bitbang_irq_config *cfg = dev->config;
	int rc;

	if (!gpio_is_ready_dt(&cfg->clk_gpio)) {
		LOG_ERR("clk gpio not ready");
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&cfg->clk_gpio, GPIO_OUTPUT_INACTIVE);
	if (rc < 0) {
		return rc;
	}

	if (cfg->mosi_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->mosi_gpio)) {
			return -ENODEV;
		}
		rc = gpio_pin_configure_dt(&cfg->mosi_gpio, GPIO_OUTPUT_INACTIVE);
		if (rc < 0) {
			return rc;
		}
	}

	if (cfg->miso_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->miso_gpio)) {
			return -ENODEV;
		}
		rc = gpio_pin_configure_dt(&cfg->miso_gpio, GPIO_INPUT);
		if (rc < 0) {
			return rc;
		}
	}

	/* No spi_context here (its k_sem is ISR-incompatible), so the
	 * driver configures each CS line as an inactive output directly.
	 */
	for (uint8_t i = 0; i < cfg->cs_count; i++) {
		const struct gpio_dt_spec *cs = &cfg->cs_gpios[i];

		if (cs->port == NULL) {
			continue;
		}
		if (!gpio_is_ready_dt(cs)) {
			LOG_ERR("cs%d gpio not ready", i);
			return -ENODEV;
		}
		rc = gpio_pin_configure_dt(cs, GPIO_OUTPUT_INACTIVE);
		if (rc < 0) {
			LOG_ERR("cs%d configure failed (%d)", i, rc);
			return rc;
		}
	}

	return 0;
}

#define SPI_BITBANG_IRQ_CS_OR(inst, i)                                                             \
	COND_CODE_1(DT_INST_PROP_HAS_IDX(inst, cs_gpios, i), \
		    (GPIO_DT_SPEC_INST_GET_BY_IDX(inst, cs_gpios, i)), \
		    ({0}))

#define SPI_BITBANG_IRQ_INIT(inst)                                                                 \
	BUILD_ASSERT(DT_INST_PROP_LEN_OR(inst, cs_gpios, 0) <= SPI_BITBANG_IRQ_MAX_CS,             \
		     "ht-radio,spi-bitbang-irq supports up to "                                    \
		     "SPI_BITBANG_IRQ_MAX_CS chip selects");                                       \
	static const struct spi_bitbang_irq_config spi_bitbang_irq_cfg_##inst = {                  \
		.clk_gpio = GPIO_DT_SPEC_INST_GET(inst, clk_gpios),                                \
		.mosi_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, mosi_gpios, {0}),                      \
		.miso_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, miso_gpios, {0}),                      \
		.cs_gpios =                                                                        \
			{                                                                          \
				SPI_BITBANG_IRQ_CS_OR(inst, 0),                                    \
				SPI_BITBANG_IRQ_CS_OR(inst, 1),                                    \
				SPI_BITBANG_IRQ_CS_OR(inst, 2),                                    \
				SPI_BITBANG_IRQ_CS_OR(inst, 3),                                    \
			},                                                                         \
		.cs_count = (uint8_t)DT_INST_PROP_LEN_OR(inst, cs_gpios, 0),                       \
	};                                                                                         \
	static struct spi_bitbang_irq_data spi_bitbang_irq_data_##inst;                            \
	SPI_DEVICE_DT_INST_DEFINE(inst, spi_bitbang_irq_init, NULL, &spi_bitbang_irq_data_##inst,  \
				  &spi_bitbang_irq_cfg_##inst, POST_KERNEL,                        \
				  CONFIG_SPI_BITBANG_IRQ_INIT_PRIORITY, &spi_bitbang_irq_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_BITBANG_IRQ_INIT)
