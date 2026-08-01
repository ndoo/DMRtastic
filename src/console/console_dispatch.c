// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include "console_dispatch.h"
#include "console_debug.h"
#include "console_view.h"
#include "console_transport.h"

#include <string.h>

#include <zephyr/kernel.h>

/** Prints every registered command's name + help line, table-driven. */
static void cmd_help(char *args)
{
	ARG_UNUSED(args);

	for (size_t i = 0; i < console_debug_cmd_count; i++) {
		console_transport_printf("%-9s %s\r\n", console_debug_cmds[i].name,
					 console_debug_cmds[i].help);
	}
	for (size_t i = 0; i < console_view_cmd_count; i++) {
		console_transport_printf("%-9s %s\r\n", console_view_cmds[i].name,
					 console_view_cmds[i].help);
	}
	console_transport_puts("help      this list\r\n");
}

static const struct console_cmd *find_cmd(const char *name)
{
	for (size_t i = 0; i < console_debug_cmd_count; i++) {
		if (strcmp(name, console_debug_cmds[i].name) == 0) {
			return &console_debug_cmds[i];
		}
	}
	for (size_t i = 0; i < console_view_cmd_count; i++) {
		if (strcmp(name, console_view_cmds[i].name) == 0) {
			return &console_view_cmds[i];
		}
	}
	return NULL;
}

/** Route one input line to its command handler by first token. */
static void dispatch(char *line)
{
	char *cmd = strtok(line, " \t");

	if (!cmd || cmd[0] == '\0') {
		return;
	}
	char *rest = strtok(NULL, "");

	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		cmd_help(rest);
		return;
	}

	const struct console_cmd *c = find_cmd(cmd);

	if (!c) {
		console_transport_printf("ERR: unknown command '%s' (try 'help')\r\n", cmd);
		return;
	}
	c->handler(rest ? rest : "");
}

#define LINE_BUF 80

/** Line-editing read loop: buffer chars until CR/LF, then dispatch(). */
static void console_dispatch_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	console_transport_rx_start();

	char buf[LINE_BUF];
	int pos = 0;

	while (true) {
		int c = console_transport_getc();

		if (c == '\r' || c == '\n') {
			if (pos > 0) {
				buf[pos] = '\0';
				pos = 0;
				dispatch(buf);
			}
		} else if (c == '\b' || c == 0x7F) {
			if (pos > 0) {
				pos--;
			}
		} else if (pos < LINE_BUF - 1) {
			buf[pos++] = (char)c;
		}
	}
}

#define CONSOLE_DISPATCH_STACK 1536
#define CONSOLE_DISPATCH_PRIO  10

K_THREAD_DEFINE(console_dispatch_tid, CONSOLE_DISPATCH_STACK, console_dispatch_thread_fn, NULL,
		NULL, NULL, CONSOLE_DISPATCH_PRIO, 0, 0);
