// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cmdline.h>
#include <posix_board_if.h>
#include <soc.h>

#include <sim/sim_ctl.h>
#include <sim/sim_host.h>

LOG_MODULE_REGISTER(sim_ctl, LOG_LEVEL_INF);

#define SIM_CTL_MAX_CMDS   32
#define SIM_CTL_MAX_ARGS   8
#define SIM_CTL_SCRIPT_MAX 8192

static struct {
	const char *name;
	sim_ctl_fn fn;
} cmds[SIM_CTL_MAX_CMDS];
static int ncmds;

static char *script_path;
static char script_buf[SIM_CTL_SCRIPT_MAX];

K_THREAD_STACK_DEFINE(script_stack, 4096);
static struct k_thread script_thread;

int sim_ctl_register(const char *name, sim_ctl_fn fn)
{
	if (ncmds >= SIM_CTL_MAX_CMDS) {
		return -ENOMEM;
	}
	cmds[ncmds].name = name;
	cmds[ncmds].fn = fn;
	ncmds++;
	return 0;
}

int sim_ctl_run_line(char *line)
{
	char *argv[SIM_CTL_MAX_ARGS];
	int argc = 0;
	char *save = NULL;
	char *tok;

	for (tok = strtok_r(line, " \t\r", &save); tok != NULL && argc < SIM_CTL_MAX_ARGS;
	     tok = strtok_r(NULL, " \t\r", &save)) {
		argv[argc++] = tok;
	}
	if (argc == 0 || argv[0][0] == '#') {
		return 0;
	}
	for (int i = 0; i < ncmds; i++) {
		if (strcmp(cmds[i].name, argv[0]) == 0) {
			return cmds[i].fn(argc, argv);
		}
	}
	return -ENOENT;
}

static int cmd_wait(int argc, char **argv)
{
	if (argc < 2) {
		return -EINVAL;
	}
	k_msleep(strtol(argv[1], NULL, 0));
	return 0;
}

static int cmd_quit(int argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	posix_exit(0);
}

static int cmd_echo(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		LOG_INF("%s", argv[i]);
	}
	return 0;
}

static void script_fn(void *p1, void *p2, void *p3)
{
	char *line;
	char *save = NULL;
	int n = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (line = strtok_r(script_buf, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		int rc;

		n++;
		rc = sim_ctl_run_line(line);
		if (rc < 0) {
			LOG_ERR("script line %d failed (%d)", n, rc);
		}
	}
	LOG_INF("script finished");
}

static int sim_ctl_start(void)
{
	long len;

	sim_ctl_register("wait", cmd_wait);
	sim_ctl_register("quit", cmd_quit);
	sim_ctl_register("echo", cmd_echo);

	if (script_path == NULL) {
		return 0;
	}
	len = sim_host_read_file(script_path, script_buf, sizeof(script_buf) - 1);
	if (len < 0) {
		LOG_ERR("cannot read script %s", script_path);
		return 0;
	}
	script_buf[len] = '\0';
	return 0;
}

/* Runs after every emulator has registered its commands. */
static int sim_ctl_launch(void)
{
	if (script_path != NULL && script_buf[0] != '\0') {
		k_thread_create(&script_thread, script_stack, K_THREAD_STACK_SIZEOF(script_stack),
				script_fn, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	}
	return 0;
}

SYS_INIT(sim_ctl_start, POST_KERNEL, 0);
SYS_INIT(sim_ctl_launch, APPLICATION, 99);

static void sim_ctl_options(void)
{
	static struct args_struct_t opts[] = {
		{
			.option = "script",
			.name = "path",
			.type = 's',
			.dest = (void *)&script_path,
			.descript = "Scenario script run in simulated time",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(opts);
}

NATIVE_TASK(sim_ctl_options, PRE_BOOT_1, 1);
