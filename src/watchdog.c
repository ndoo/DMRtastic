// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/gpio.h>

#define WDT_TIMER_PERIOD_MS 50
#define WDT_LED_TICKS       (1000 / WDT_TIMER_PERIOD_MS)

static const struct device *wdt_dev = DEVICE_DT_GET(DT_NODELABEL(iwdg));
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static uint32_t wdt_tick;

/** Feed the IWDG and blink led0 at 1 Hz as a liveness indicator. */
static void wdt_feed_timer_cb(struct k_timer *timer)
{
	wdt_feed(wdt_dev, 0);
	if (wdt_tick == 0) {
		gpio_pin_set_dt(&led0, 1);
	} else if (wdt_tick == 1) {
		gpio_pin_set_dt(&led0, 0);
	}
	if (++wdt_tick >= WDT_LED_TICKS) {
		wdt_tick = 0;
	}
}

K_TIMER_DEFINE(wdt_feed_timer, wdt_feed_timer_cb, NULL);

static int watchdog_init(void)
{
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	wdt_feed(wdt_dev, 0);
	k_timer_start(&wdt_feed_timer, K_MSEC(WDT_TIMER_PERIOD_MS), K_MSEC(WDT_TIMER_PERIOD_MS));
	return 0;
}

SYS_INIT(watchdog_init, POST_KERNEL, 60);
