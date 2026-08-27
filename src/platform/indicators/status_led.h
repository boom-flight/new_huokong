/**
 * @file status_led.h
 * @brief Platform contract for the board status LED.
 */

#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>

/**
 * @brief Initialize the board status LED output.
 */
void status_led_init(void);

/**
 * @brief Set the board status LED state.
 * @param on true to turn the LED on, false to turn it off.
 */
void status_led_set(bool on);

#endif
