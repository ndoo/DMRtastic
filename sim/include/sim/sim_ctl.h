/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Scenario control: a script file (--script=<path>) of one command per line,
 * run on a simulator thread in simulated time. Emulators register commands.
 */

#ifndef DMRTASTIC_SIM_SIM_CTL_H_
#define DMRTASTIC_SIM_SIM_CTL_H_

/** Command handler; argv[0] is the command name. Returns 0 or a negative errno. */
typedef int (*sim_ctl_fn)(int argc, char **argv);

int sim_ctl_register(const char *name, sim_ctl_fn fn);

/** Split line into words and run it. Returns the handler's result or -ENOENT. */
int sim_ctl_run_line(char *line);

#endif /* DMRTASTIC_SIM_SIM_CTL_H_ */
