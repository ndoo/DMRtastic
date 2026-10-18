// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#define DT_DRV_COMPAT dmrtastic_sim_pwm

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

#define SIM_PWM_CYCLES_PER_SEC 1000000U

static int sim_pwm_set_cycles(const struct device *dev, uint32_t channel, uint32_t period,
			      uint32_t pulse, pwm_flags_t flags)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(period);
	ARG_UNUSED(pulse);
	ARG_UNUSED(flags);
	return 0;
}

static int sim_pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
				      uint64_t *cycles)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	*cycles = SIM_PWM_CYCLES_PER_SEC;
	return 0;
}

static DEVICE_API(pwm, sim_pwm_api) = {
	.set_cycles = sim_pwm_set_cycles,
	.get_cycles_per_sec = sim_pwm_get_cycles_per_sec,
};

#define SIM_PWM_INIT(n)                                                                            \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &sim_pwm_api);

DT_INST_FOREACH_STATUS_OKAY(SIM_PWM_INIT)
