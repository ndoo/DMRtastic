// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Andrew Yong <me@ndoo.sg>

/* GPS module model: NMEA sentences into the USART1 receive FIFO while the enable pin is high. */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <sim/sim_ctl.h>
#include <sim/sim_gpio.h>

#define GPS_UART DEVICE_DT_GET(DT_NODELABEL(usart1))
#define GPS_EN   GPIO_DT_SPEC_GET(DT_ALIAS(gps_en), gpios)

#define SENTENCE_PERIOD_MS 1000
#define EPOCH_SECONDS      (12 * 3600)

static const struct gpio_dt_spec s_en = GPS_EN;

static double s_lat = 1.3521;
static double s_lon = 103.8198;
static bool s_fix;

static void send_sentence(const char *body)
{
	char line[128];
	uint8_t sum = 0;
	int n;

	for (const char *p = body; *p != '\0'; p++) {
		sum ^= (uint8_t)*p;
	}
	n = snprintf(line, sizeof(line), "$%s*%02X\r\n", body, sum);
	uart_emul_put_rx_data(GPS_UART, (const uint8_t *)line, n);
}

static void format_coord(char *out, size_t len, double deg, int width, char pos, char neg)
{
	double a = fabs(deg);
	int d = (int)a;
	double m = (a - d) * 60.0;

	snprintf(out, len, "%0*.4f,%c", width, d * 100 + m, deg >= 0 ? pos : neg);
}

static void gps_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_work, gps_tick);

static void gps_tick(struct k_work *work)
{
	char lat[24];
	char lon[24];
	char body[112];
	uint32_t t = EPOCH_SECONDS + k_uptime_get() / 1000;

	ARG_UNUSED(work);
	k_work_schedule(&s_work, K_MSEC(SENTENCE_PERIOD_MS));

	if (!(sim_gpio_out_latch(s_en.port) & sim_gpio_out_enable(s_en.port) & BIT(s_en.pin))) {
		return;
	}

	format_coord(lat, sizeof(lat), s_lat, 9, 'N', 'S');
	format_coord(lon, sizeof(lon), s_lon, 10, 'E', 'W');
	snprintf(body, sizeof(body), "GPRMC,%02u%02u%02u.00,%c,%s,%s,0.0,0.0,010126,,,%c",
		 t / 3600 % 24, t / 60 % 60, t % 60, s_fix ? 'A' : 'V', lat, lon, s_fix ? 'A' : 'N');
	send_sentence(body);
	snprintf(body, sizeof(body), "GPGGA,%02u%02u%02u.00,%s,%s,%d,%02d,0.9,15.0,M,0.0,M,,",
		 t / 3600 % 24, t / 60 % 60, t % 60, lat, lon, s_fix ? 1 : 0, s_fix ? 8 : 0);
	send_sentence(body);
}

/* "gps fix [lat lon]" starts reporting a fix, "gps nofix" reports none. */
static int cmd_gps(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "nofix") == 0) {
		s_fix = false;
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "fix") == 0) {
		if (argc >= 4) {
			s_lat = strtod(argv[2], NULL);
			s_lon = strtod(argv[3], NULL);
		}
		s_fix = true;
		return 0;
	}
	return -EINVAL;
}

static int sim_gps_init(void)
{
	sim_ctl_register("gps", cmd_gps);
	k_work_schedule(&s_work, K_MSEC(SENTENCE_PERIOD_MS));
	return 0;
}

SYS_INIT(sim_gps_init, APPLICATION, 50);
