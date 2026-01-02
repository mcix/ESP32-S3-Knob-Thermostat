#ifndef UI_H
#define UI_H

#include <lvgl.h>

/**
 * UI State enum
 */
enum UIState {
    UI_STATE_CONNECTING,
    UI_STATE_NORMAL,
    UI_STATE_ADJUSTING,
    UI_STATE_SENDING,
    UI_STATE_ERROR
};

/**
 * Initialize the thermostat UI
 * Creates all UI elements on the display
 */
void ui_init();

/**
 * Update target temperature display
 * @param temp Temperature in Celsius
 */
void ui_set_target_temp(float temp);

/**
 * Update current temperature display
 * @param temp Temperature in Celsius
 */
void ui_set_current_temp(float temp);

/**
 * Set UI state
 * @param state Current UI state
 */
void ui_set_state(UIState state);

/**
 * Show error message
 * @param message Error message to display
 */
void ui_show_error(const char* message);

/**
 * Clear error message
 */
void ui_clear_error();

/**
 * Get the temperature arc widget for encoder group
 * @return pointer to arc widget
 */
lv_obj_t* ui_get_arc();

/**
 * Animate temperature change confirmation
 */
void ui_animate_confirm();

/**
 * Update arc position to match temperature
 * @param temp Temperature value
 */
void ui_update_arc(float temp);

#endif // UI_H
