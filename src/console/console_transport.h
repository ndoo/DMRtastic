// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef DMRTASTIC_CONSOLE_TRANSPORT_H_
#define DMRTASTIC_CONSOLE_TRANSPORT_H_

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USB CDC-ACM transport: owns the one physical UART (zephyr_cdc_acm_uart) and the
 * USBD device/descriptors. Platform/system level, same carve-out as display.c --
 * nothing outside src/console/ may touch this UART device or the USBD stack.
 *
 * TX is mutex-guarded so the log backend (console_log_backend.c) and interactive
 * command output (console_dispatch.c and friends) can never interleave mid-line. */

void console_transport_write(const void *data, size_t len);
void console_transport_putc(char c);
void console_transport_puts(const char *s);
void console_transport_vprintf(const char *fmt, va_list ap);
void console_transport_printf(const char *fmt, ...);

/* Enables the UART RX interrupt and installs the ISR; call once, from the console
 * dispatch thread (the only RX consumer) before its first console_transport_getc(). */
void console_transport_rx_start(void);

/* Blocks until a byte is available. */
int console_transport_getc(void);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CONSOLE_TRANSPORT_H_ */
