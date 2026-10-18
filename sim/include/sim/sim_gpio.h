/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Andrew Yong <me@ndoo.sg>
 *
 * Simulated 16-pin GPIO port. Output changes notify one listener; input levels come from a
 * peer callback, forced levels, or the configured pull.
 */

#ifndef DMRTASTIC_SIM_SIM_GPIO_H_
#define DMRTASTIC_SIM_SIM_GPIO_H_

#include <stdint.h>
#include <zephyr/device.h>

/** Called after any output latch or direction change on a port. */
typedef void (*sim_gpio_notify_fn)(const struct device *port, void *ctx);

/** Returns the levels of the pins in *mask that the peer currently drives. */
typedef uint32_t (*sim_gpio_peer_fn)(void *ctx, uint32_t *mask);

void sim_gpio_set_listener(const struct device *port, sim_gpio_notify_fn fn, void *ctx);
void sim_gpio_set_peer(const struct device *port, sim_gpio_peer_fn fn, void *ctx);

/** Pin levels as seen on the wire: output latch for outputs, resolved level for inputs. */
uint32_t sim_gpio_wire(const struct device *port);

/** Lock-free views of the output latch and the output-enable mask. */
uint32_t sim_gpio_out_latch(const struct device *port);
uint32_t sim_gpio_out_enable(const struct device *port);

/** Force the pins in mask to the given levels on the input side. */
void sim_gpio_force(const struct device *port, uint32_t mask, uint32_t levels);
void sim_gpio_unforce(const struct device *port, uint32_t mask);

/** Re-evaluate input levels and fire interrupt callbacks for changed pins. */
void sim_gpio_poke(const struct device *port);

#endif /* DMRTASTIC_SIM_SIM_GPIO_H_ */
