#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

/**
 * Initialize the CST816 touch controller
 * @return true if initialization successful
 */
bool touch_init();

/**
 * Read touch state
 * @param x pointer to store x coordinate
 * @param y pointer to store y coordinate
 * @return true if touch is active
 */
bool touch_read(uint16_t* x, uint16_t* y);

/**
 * Check if screen was tapped (touch and release)
 * Call this in main loop to detect taps
 * @return true if a tap was detected
 */
bool touch_tapped();

#endif // TOUCH_H
