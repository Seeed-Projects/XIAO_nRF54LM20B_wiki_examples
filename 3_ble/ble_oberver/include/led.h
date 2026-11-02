/*
 * RGB LED status indicator for XIAO nRF54LM20B BLE demos.
 * ----------------------------------------------------------------------------
 * The board has a common-anode RGB LED driven by gpio-leds (active-low):
 *   red_led = gpio1.22, blue_led = gpio1.23, green_led = gpio1.24
 * Because the DTS marks them GPIO_ACTIVE_LOW, gpio_pin_set_dt(spec, 1) lights
 * the LED (logical active) and 0 turns it off -- the inversion is handled by
 * the GPIO driver from the DT flags, so this module works in logical values.
 *
 * A background thread maps a small set of demo states onto blink patterns so
 * every BLE demo gets a consistent visual vocabulary:
 *   blue slow  = scanning / advertising (looking for a peer)
 *   blue fast  = transient event (device found / GATT write sent)
 *   blue on    = CS procedure running
 *   green on   = connected
 *   green fast = write success burst
 *   yellow on  = CS medium distance (red+green)
 *   red on     = error
 *   red slow   = CS failed / out of range
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LED_H_
#define LED_H_

#include <zephyr/kernel.h>

enum led_state {
	LED_OFF = 0,
	LED_BLUE_SLOW,	/* scanning / advertising            */
	LED_BLUE_FAST,	/* device found / write sent         */
	LED_BLUE_ON,	/* CS procedure running              */
	LED_GREEN_ON,	/* connected                         */
	LED_GREEN_FAST,	/* write success burst               */
	LED_RED_ON,	/* error                             */
	LED_RED_SLOW,	/* CS failed / out of range          */
	LED_YELLOW_ON,	/* CS medium distance (red + green)  */
};

/* Configure the three GPIO pins and start the blink thread. Safe to call once. */
void led_init(void);

/* Switch the running blink pattern. Takes effect within one blink slot. */
void led_set_state(enum led_state s);

#endif /* LED_H_ */
