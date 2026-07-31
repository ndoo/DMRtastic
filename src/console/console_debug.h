// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef DMRTASTIC_CONSOLE_DEBUG_H_
#define DMRTASTIC_CONSOLE_DEBUG_H_

#include <stddef.h>

#include "console_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware-direct debug commands (at/hc/rssi/rtc/cp/reboot) -- register-level
 * poking for bring-up, backed only by model/radio_debug.h and model/codeplug.h. */
extern const struct console_cmd console_debug_cmds[];
extern const size_t console_debug_cmd_count;

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CONSOLE_DEBUG_H_ */
