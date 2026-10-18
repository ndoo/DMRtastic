// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#define DT_DRV_COMPAT dmrtastic_sim_wdt

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>

static int sim_wdt_setup(const struct device *dev, uint8_t options)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(options);
	return 0;
}

static int sim_wdt_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int sim_wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cfg);
	return 0;
}

static int sim_wdt_feed(const struct device *dev, int channel_id)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	return 0;
}

static DEVICE_API(wdt, sim_wdt_api) = {
	.setup = sim_wdt_setup,
	.disable = sim_wdt_disable,
	.install_timeout = sim_wdt_install_timeout,
	.feed = sim_wdt_feed,
};

#define SIM_WDT_INIT(n)                                                                            \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &sim_wdt_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_WDT_INIT)
