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

/**
 * Set heating state (changes background color)
 * @param heating True if central heating is active
 */
void ui_set_heating(bool heating);

/**
 * Show status text (small text below current temp)
 * @param text Status text to display
 */
void ui_show_status(const char* text);

/**
 * Show the outside temperature and weather condition icon
 * @param temp Outside temperature in Celsius
 * @param wmo_code WMO weather interpretation code (selects the icon)
 */
void ui_set_weather(float temp, int wmo_code);

/**
 * Clear status text
 */
void ui_clear_status();

/**
 * Set timer to auto-clear status text
 * @param delay_ms Delay in milliseconds before clearing
 */
void ui_set_status_clear_timer(uint32_t delay_ms);

/**
 * Update UI (call from main loop to handle timers)
 */
void ui_update();

/**
 * Set visibility of thermostat UI elements
 * @param visible True to show, false to hide
 */
void ui_set_visible(bool visible);

/**
 * Get the thermostat UI container
 * @return pointer to container object
 */
lv_obj_t* ui_get_container();

#endif // UI_H
