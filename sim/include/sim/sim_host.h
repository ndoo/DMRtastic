/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Host file access for the emulators. Implemented in the native_sim runner
 * context (drivers/sim_host.c), where the host C library is available.
 */

#ifndef DMRTASTIC_SIM_SIM_HOST_H_
#define DMRTASTIC_SIM_SIM_HOST_H_

#include <stddef.h>

/** Create or truncate path and write len bytes. Returns 0 or -1. */
int sim_host_write_file(const char *path, const void *buf, size_t len);

/** Read up to max bytes of path into buf. Returns the byte count or -1. */
long sim_host_read_file(const char *path, void *buf, size_t max);

/** Print a string on the host console. */
void sim_host_puts(const char *s);

/** Size of path in bytes, or -1. */
long sim_host_file_size(const char *path);

#endif /* DMRTASTIC_SIM_SIM_HOST_H_ */
