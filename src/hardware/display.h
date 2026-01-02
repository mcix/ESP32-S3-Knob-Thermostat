#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>

/**
 * Initialize the display and LVGL
 * Sets up the LCD, touch controller, and LVGL drivers
 * @return true if initialization successful
 */
bool display_init();

/**
 * Update LVGL timer handler
 * Call this in the main loop
 */
void display_update();

/**
 * Set display brightness
 * @param brightness 0-255
 */
void display_set_brightness(uint8_t brightness);

/**
 * Get the LVGL display object
 * @return pointer to lv_disp_t
 */
lv_disp_t* display_get();

#endif // DISPLAY_H
