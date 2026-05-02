// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "usb_cdc.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	LOG_INF("MAIN");

	ret = usb_cdc_init();
	if (ret != 0) {
		LOG_ERR("USB init failed (%d)", ret);
		return ret;
	}

	LOG_INF("Wait for DTR");
	usb_cdc_wait_dtr();
	LOG_INF("DTR set");

	usb_cdc_flush_and_enable();

	return 0;
}
