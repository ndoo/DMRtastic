/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_CONSOLE_DISPATCH_H_
#define DMRTASTIC_CONSOLE_DISPATCH_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*console_cmd_fn_t)(char *args);

struct console_cmd {
	const char *name;
	const char *help; /* one line, printed by the auto-generated "help" command */
	console_cmd_fn_t handler;
};

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_CONSOLE_DISPATCH_H_ */
