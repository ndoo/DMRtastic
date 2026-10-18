/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_SIM_SIM_PNG_H_
#define DMRTASTIC_SIM_SIM_PNG_H_

#include <stdint.h>

/** Write a w x h RGB888 image as an uncompressed PNG. Returns 0 or a negative errno. */
int sim_png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

#endif /* DMRTASTIC_SIM_SIM_PNG_H_ */
