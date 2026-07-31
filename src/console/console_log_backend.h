// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef DMRTASTIC_CONSOLE_LOG_BACKEND_H_
#define DMRTASTIC_CONSOLE_LOG_BACKEND_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Releases the log drain thread to start flushing to the UART; called once by
 * console_transport.c after DTR is up, so boot-time logs are buffered for replay
 * instead of lost while the host hasn't opened the port yet. */
void console_log_backend_notify_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CONSOLE_LOG_BACKEND_H_ */
