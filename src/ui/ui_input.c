// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
// SPDX-License-Identifier: MIT

/*
 * Input bridge — the only place raw Zephyr input events turn into ui_action_t.
 * Sources: keypad matrix + SK1/SK2, PTT gpio-keys, rotary encoder, volume pot.
 */

#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "ui.h"

LOG_MODULE_DECLARE(app_ui, LOG_LEVEL_DBG);

#ifndef INPUT_KEY_NUMERIC_STAR
#define INPUT_KEY_NUMERIC_STAR  0x20a
#endif
#ifndef INPUT_KEY_NUMERIC_POUND
#define INPUT_KEY_NUMERIC_POUND 0x20b
#endif

#define DMRTASTIC_KEY_PTT     INPUT_KEY_F13 /* PE11, ptt_buttons */
#define DMRTASTIC_KEY_PTT_EXT INPUT_KEY_F14 /* PE12, ptt_buttons */
#define DMRTASTIC_KEY_SK1     INPUT_KEY_F15 /* keypad matrix row2/col7 */
#define DMRTASTIC_KEY_SK2     INPUT_KEY_F16 /* keypad matrix row2/col6 */

#define SK1_LONG_PRESS_MS 600

static int64_t s_sk1_press_uptime;

/* Volume pot hysteresis: reports a new raw reading only once it moves far enough from the last applied one,
 * with a direction-aware exception so the range extremes stay reachable. See README for the full rationale. */
#define VOLUME_RAW_MIN     DT_PROP(DT_NODELABEL(vol_axis_ch), in_min)
#define VOLUME_RAW_MAX     DT_PROP(DT_NODELABEL(vol_axis_ch), in_max)
#define VOLUME_HYSTERESIS  ((VOLUME_RAW_MAX - VOLUME_RAW_MIN) / 100)
#define VOLUME_RAW_UNSET   0xFFFFU

BUILD_ASSERT(VOLUME_RAW_MAX < VOLUME_RAW_UNSET, "vol_axis_ch in-max too large for sentinel");

static uint16_t s_last_stable_vol_raw = VOLUME_RAW_UNSET;

static bool digit_action_for(uint16_t code, ui_action_t *action)
{
	switch (code) {
	case INPUT_KEY_0: *action = UI_ACTION_KEY_0; return true;
	case INPUT_KEY_1: *action = UI_ACTION_KEY_1; return true;
	case INPUT_KEY_2: *action = UI_ACTION_KEY_2; return true;
	case INPUT_KEY_3: *action = UI_ACTION_KEY_3; return true;
	case INPUT_KEY_4: *action = UI_ACTION_KEY_4; return true;
	case INPUT_KEY_5: *action = UI_ACTION_KEY_5; return true;
	case INPUT_KEY_6: *action = UI_ACTION_KEY_6; return true;
	case INPUT_KEY_7: *action = UI_ACTION_KEY_7; return true;
	case INPUT_KEY_8: *action = UI_ACTION_KEY_8; return true;
	case INPUT_KEY_9: *action = UI_ACTION_KEY_9; return true;
	case INPUT_KEY_NUMERIC_STAR:  *action = UI_ACTION_KEY_STAR;  return true;
	case INPUT_KEY_NUMERIC_POUND: *action = UI_ACTION_KEY_POUND; return true;
	default: return false;
	}
}

/** Translates a keypad/SK/PTT input event into ui_action_t, handling SK1's long-press debounce. */
static void handle_key_event(const struct input_event *evt)
{
	bool pressed = evt->value != 0;
	ui_action_t action;

	switch (evt->code) {
	case INPUT_KEY_ENTER:
		if (pressed) {
			ui_post_action(UI_ACTION_OK);
		}
		return;
	case INPUT_KEY_UP:
		if (pressed) {
			ui_post_action(UI_ACTION_UP);
		}
		return;
	case INPUT_KEY_DOWN:
		if (pressed) {
			ui_post_action(UI_ACTION_DOWN);
		}
		return;
	case INPUT_KEY_BACK:
		if (pressed) {
			ui_post_action(UI_ACTION_BACK);
		}
		return;
	case DMRTASTIC_KEY_SK2:
		if (pressed) {
			ui_post_action(UI_ACTION_SK2);
		}
		return;
	case DMRTASTIC_KEY_SK1:
		if (pressed) {
			s_sk1_press_uptime = k_uptime_get();
		} else if (k_uptime_get() - s_sk1_press_uptime >= SK1_LONG_PRESS_MS) {
			ui_post_action(UI_ACTION_SK1);
		}
		return;
	case DMRTASTIC_KEY_PTT:
	case DMRTASTIC_KEY_PTT_EXT:
		ui_post_action(pressed ? UI_ACTION_PTT : UI_ACTION_PTT_RELEASE);
		return;
	default:
		break;
	}

	if (pressed && digit_action_for(evt->code, &action)) {
		ui_post_action(action);
	}
}

/** Zephyr input callback: dispatches key, encoder, and volume-pot events to ui_action_t. */
static void ui_input_event_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type == INPUT_EV_KEY && evt->value != 0) {
		LOG_DBG("key event: code=%u value=%d sync=%d",
			evt->code, evt->value, evt->sync);
	}

	switch (evt->type) {
	case INPUT_EV_KEY:
		handle_key_event(evt);
		break;
	case INPUT_EV_REL:
		if (evt->code == INPUT_REL_Y) {
			/* evt->value is the net quadrature count (whole detents) since the
			 * last report; post one action per detent rather than dropping excess. */
			ui_action_t action = evt->value > 0 ? UI_ACTION_ENCODER_CCW
							    : UI_ACTION_ENCODER_CW;
			int32_t count = evt->value < 0 ? -evt->value : evt->value;

			for (int32_t i = 0; i < count; i++) {
				ui_post_action(action);
			}
		}
		break;
	case INPUT_EV_ABS:
		if (evt->code == INPUT_ABS_THROTTLE) {
			uint16_t raw = (uint16_t)evt->value;
			int diff = (int)raw - (int)s_last_stable_vol_raw;
			bool climbing_to_max = diff > 0 &&
				raw >= VOLUME_RAW_MAX - VOLUME_HYSTERESIS;
			bool climbing_to_min = diff < 0 &&
				raw <= VOLUME_RAW_MIN + VOLUME_HYSTERESIS;

			LOG_DBG("vol_axis raw: %u (%u-%u)", raw, VOLUME_RAW_MIN, VOLUME_RAW_MAX);

			if (diff < 0) {
				diff = -diff;
			}
			if (s_last_stable_vol_raw == VOLUME_RAW_UNSET ||
			    diff >= VOLUME_HYSTERESIS ||
			    climbing_to_max || climbing_to_min) {
				s_last_stable_vol_raw = raw;
				ui_post_volume_abs(raw);
			}
		}
		break;
	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, ui_input_event_cb, NULL);
