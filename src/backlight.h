/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 */

#ifndef DMRTASTIC_BACKLIGHT_H_
#define DMRTASTIC_BACKLIGHT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Forces the backlight fully off immediately (no fade) so the panel doesn't sit at the
 * driver's full-brightness boot default while the rest of app_init() runs. Call once, as
 * early as possible in app_init() -- before any screen/settings setup.
 */
void backlight_init(void);

/** Fades the PWM backlight to pct (0-100) with a sinusoidal ease curve. slow selects a
 * ~10 s fade, reserved for the automatic idle-timeout dim-out; every other caller
 * (settings changes, undimming on activity, and the initial fade-up from
 * backlight_init()'s 0 once settings load) should pass false for a quick fade.
 */
void backlight_set_pct(uint8_t pct, bool slow);

#ifdef __cplusplus
}
#endif

#endif /* DMRTASTIC_BACKLIGHT_H_ */
