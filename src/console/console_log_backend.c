// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "console_log_backend.h"
#include "console_transport.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>

/* Zephyr log backend over the console UART. Log path is non-blocking: buffered here
 * (drop-oldest when full), drained by console_log_drain_thread_fn() through
 * console_transport's mutex-guarded TX path -- shared with interactive command
 * output, so a log line can never interleave mid-line with command output.
 */

#define CDC_LOG_BUF_SIZE 2048
RING_BUF_DECLARE(cdc_log_ringbuf, CDC_LOG_BUF_SIZE);
static atomic_t cdc_log_ready; /* 0 before DTR, 1 after -- gates the drain thread */

/** Ring-buffer log sink for the CDC backend; drops oldest bytes when full. */
static int cdc_log_out_func(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	uint32_t space = ring_buf_space_get(&cdc_log_ringbuf);

	if (space < length) {
		uint8_t discard[64];
		uint32_t to_drop = length - space;

		while (to_drop > 0) {
			uint32_t n = MIN(to_drop, sizeof(discard));

			ring_buf_get(&cdc_log_ringbuf, discard, n);
			to_drop -= n;
		}
	}
	ring_buf_put(&cdc_log_ringbuf, data, length);
	return (int)length;
}

static uint8_t cdc_log_out_scratch[128];
LOG_OUTPUT_DEFINE(cdc_log_output, cdc_log_out_func, cdc_log_out_scratch,
		  sizeof(cdc_log_out_scratch));

static void cdc_backend_process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);
	uint32_t flags = LOG_OUTPUT_FLAG_TIMESTAMP | LOG_OUTPUT_FLAG_LEVEL;

	log_output_msg_process(&cdc_log_output, &msg->log, flags);
}

static const struct log_backend_api cdc_backend_api = {
	.process = cdc_backend_process,
};

LOG_BACKEND_DEFINE(cdc_backend, cdc_backend_api, true);

void console_log_backend_notify_ready(void)
{
	atomic_set(&cdc_log_ready, 1);
}

/** Drains cdc_log_ringbuf to the console UART once ready; parks pre-ready at 100ms. */
static void console_log_drain_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t buf[64];

	while (true) {
		if (!atomic_get(&cdc_log_ready)) {
			k_msleep(100);
			continue;
		}

		uint32_t n = ring_buf_get(&cdc_log_ringbuf, buf, sizeof(buf));

		if (n == 0) {
			k_msleep(10);
			continue;
		}

		console_transport_write(buf, n);
	}
}

#define CONSOLE_LOG_DRAIN_THREAD_STACK_SIZE 1024
#define CONSOLE_LOG_DRAIN_THREAD_PRIO       9

K_THREAD_DEFINE(console_log_drain_thread_id, CONSOLE_LOG_DRAIN_THREAD_STACK_SIZE,
		console_log_drain_thread_fn, NULL, NULL, NULL, CONSOLE_LOG_DRAIN_THREAD_PRIO, 0, 0);
