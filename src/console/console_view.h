// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

#ifndef DMRTASTIC_CONSOLE_VIEW_H_
#define DMRTASTIC_CONSOLE_VIEW_H_

#include <stddef.h>

#include "console_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User/state-facing commands (status/settings/css) -- text renderers over the same
 * Controller/Model getters and setters the LVGL screens use. No device.h, no
 * DEVICE_DT_GET; hardware access happens only inside the Controller/Model calls. */
extern const struct console_cmd console_view_cmds[];
extern const size_t console_view_cmd_count;

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CONSOLE_VIEW_H_ */
