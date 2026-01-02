#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/**
 * Callback function type for encoder events
 * @param direction: 1 for clockwise, -1 for counter-clockwise
 */
typedef void (*encoder_callback_t)(int8_t direction);

/**
 * Callback function type for button events
 * @param pressed: true if button pressed, false if released
 */
typedef void (*button_callback_t)(bool pressed);

/**
 * Initialize the rotary encoder
 * @return true if initialization successful
 */
bool encoder_init();

/**
 * Set callback for rotation events
 * @param callback Function to call on rotation
 */
void encoder_set_rotation_callback(encoder_callback_t callback);

/**
 * Set callback for button press events
 * @param callback Function to call on button press/release
 */
void encoder_set_button_callback(button_callback_t callback);

/**
 * Get current encoder position (cumulative)
 * @return encoder position
 */
int32_t encoder_get_position();

/**
 * Reset encoder position to zero
 */
void encoder_reset_position();

/**
 * Check if button is currently pressed
 * @return true if pressed
 */
bool encoder_button_pressed();

/**
 * Update encoder state (call in main loop if not using interrupts)
 */
void encoder_update();

/**
 * Register encoder as LVGL input device
 * Links encoder rotation to LVGL group navigation
 */
void encoder_register_lvgl();

#endif // ENCODER_H
