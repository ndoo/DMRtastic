// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/* Built in the native_sim runner context, not the embedded one. */

#include <stdio.h>
#include <stddef.h>
#include <sys/stat.h>

#include <nsi_tracing.h>

int sim_host_write_file(const char *path, const void *buf, size_t len)
{
	FILE *f = fopen(path, "wb");
	size_t n;

	if (f == NULL) {
		nsi_print_warning("sim: cannot open %s for writing\n", path);
		return -1;
	}
	n = fwrite(buf, 1, len, f);
	fclose(f);
	return n == len ? 0 : -1;
}

long sim_host_read_file(const char *path, void *buf, size_t max)
{
	FILE *f = fopen(path, "rb");
	size_t n;

	if (f == NULL) {
		return -1;
	}
	n = fread(buf, 1, max, f);
	fclose(f);
	return (long)n;
}

void sim_host_puts(const char *s)
{
	fputs(s, stdout);
	fflush(stdout);
}

long sim_host_file_size(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0) {
		return -1;
	}
	return (long)st.st_size;
}
