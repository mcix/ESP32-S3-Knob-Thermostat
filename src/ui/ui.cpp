#include "ui.h"
#include "../config.h"
#include <Arduino.h>
#include <stdio.h>

// UI Elements
static lv_obj_t* screen = nullptr;
static lv_obj_t* arc_bg = nullptr;      // Background arc (full circle)
static lv_obj_t* arc_temp = nullptr;    // Temperature arc (shows value)
static lv_obj_t* label_target = nullptr;
static lv_obj_t* label_current = nullptr;
static lv_obj_t* label_status = nullptr;
static lv_obj_t* spinner = nullptr;

// Current values
static float current_target = TEMP_DEFAULT;
static UIState current_state = UI_STATE_CONNECTING;

// Colors - Note: Display uses invertDisplay(true), so colors appear as intended
static const lv_color_t COLOR_BG = lv_color_black();            // Pure black background
static const lv_color_t COLOR_ARC_BG = lv_color_hex(0x333333);  // Dark gray arc background
static const lv_color_t COLOR_ARC_COLD = lv_color_hex(0x4facfe);
static const lv_color_t COLOR_ARC_WARM = lv_color_hex(0xf5576c);
static const lv_color_t COLOR_ARC_NORMAL = lv_color_hex(0x00f2fe);
static const lv_color_t COLOR_TEXT = lv_color_white();          // White text
static const lv_color_t COLOR_TEXT_DIM = lv_color_hex(0x888888);
static const lv_color_t COLOR_SUCCESS = lv_color_hex(0x4ade80);

// Convert temperature to arc angle (15-30°C -> 135-405 degrees = 270 degree range)
static int16_t temp_to_angle(float temp) {
    float normalized = (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    return (int16_t)(135 + normalized * 270);
}

// Get arc color based on temperature
static lv_color_t get_temp_color(float temp) {
    if (temp < 18.0f) return COLOR_ARC_COLD;
    if (temp > 23.0f) return COLOR_ARC_WARM;
    return COLOR_ARC_NORMAL;
}

void ui_init() {
    // Get active screen and set black background
    screen = lv_scr_act();

    // Force black background by removing all styles and setting explicitly
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    Serial.println("UI: Screen background set to black");

    // Create background arc (full range indicator)
    arc_bg = lv_arc_create(screen);
    lv_obj_set_size(arc_bg, 320, 320);
    lv_obj_center(arc_bg);
    lv_arc_set_mode(arc_bg, LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_angles(arc_bg, 135, 405);
    lv_arc_set_angles(arc_bg, 135, 405);
    lv_arc_set_range(arc_bg, TEMP_MIN * 10, TEMP_MAX * 10);
    lv_obj_remove_style(arc_bg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_bg, COLOR_ARC_BG, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_bg, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_bg, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_bg, 20, LV_PART_MAIN);

    // Create temperature arc (value indicator)
    arc_temp = lv_arc_create(screen);
    lv_obj_set_size(arc_temp, 320, 320);
    lv_obj_center(arc_temp);
    lv_arc_set_mode(arc_temp, LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_angles(arc_temp, 135, 405);
    lv_arc_set_range(arc_temp, TEMP_MIN * 10, TEMP_MAX * 10);
    lv_arc_set_value(arc_temp, TEMP_DEFAULT * 10);
    lv_obj_remove_style(arc_temp, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_temp, COLOR_ARC_NORMAL, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_temp, lv_color_hex(0x00000000), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_temp, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_temp, true, LV_PART_INDICATOR);

    // Target temperature label (large, center)
    label_target = lv_label_create(screen);
    lv_obj_set_style_text_font(label_target, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label_target, COLOR_TEXT, 0);
    lv_label_set_text(label_target, "--.-°");
    lv_obj_align(label_target, LV_ALIGN_CENTER, 0, -20);

    // Current temperature label (smaller, below target)
    label_current = lv_label_create(screen);
    lv_obj_set_style_text_font(label_current, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_current, COLOR_TEXT_DIM, 0);
    lv_label_set_text(label_current, "Current: --.-°C");
    lv_obj_align(label_current, LV_ALIGN_CENTER, 0, 30);

    // Status label (bottom)
    label_status = lv_label_create(screen);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_status, COLOR_TEXT_DIM, 0);
    lv_label_set_text(label_status, "Turn knob to adjust");
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 70);

    // Loading spinner (hidden by default)
    spinner = lv_spinner_create(screen, 1000, 60);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_center(spinner);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    Serial.println("UI initialized");
}

void ui_set_target_temp(float temp) {
    current_target = temp;

    // Update label
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", temp);
    lv_label_set_text(label_target, buf);
    lv_obj_align(label_target, LV_ALIGN_CENTER, 0, -20);

    // Update arc
    ui_update_arc(temp);
}

void ui_set_current_temp(float temp) {
    char buf[24];
    snprintf(buf, sizeof(buf), "Current: %.1f°C", temp);
    lv_label_set_text(label_current, buf);
    lv_obj_align(label_current, LV_ALIGN_CENTER, 0, 30);
}

void ui_update_arc(float temp) {
    // Set arc value
    lv_arc_set_value(arc_temp, (int16_t)(temp * 10));

    // Update color based on temperature
    lv_obj_set_style_arc_color(arc_temp, get_temp_color(temp), LV_PART_INDICATOR);
}

void ui_set_state(UIState state) {
    current_state = state;

    switch (state) {
        case UI_STATE_CONNECTING:
            lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(label_target, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(label_current, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label_status, "Connecting...");
            lv_obj_set_style_text_color(label_status, COLOR_TEXT_DIM, 0);
            break;

        case UI_STATE_NORMAL:
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(label_target, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(label_current, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label_status, "Turn knob to adjust");
            lv_obj_set_style_text_color(label_status, COLOR_TEXT_DIM, 0);
            break;

        case UI_STATE_ADJUSTING:
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(label_target, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label_status, "Adjusting...");
            lv_obj_set_style_text_color(label_status, COLOR_ARC_NORMAL, 0);
            break;

        case UI_STATE_SENDING:
            lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label_status, "Sending to Homey...");
            lv_obj_set_style_text_color(label_status, COLOR_TEXT_DIM, 0);
            break;

        case UI_STATE_ERROR:
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(label_target, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(label_status, COLOR_ARC_WARM, 0);
            break;
    }

    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 70);
}

void ui_show_error(const char* message) {
    lv_label_set_text(label_status, message);
    lv_obj_set_style_text_color(label_status, COLOR_ARC_WARM, 0);
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 70);
}

void ui_clear_error() {
    ui_set_state(UI_STATE_NORMAL);
}

lv_obj_t* ui_get_arc() {
    return arc_temp;
}

// Animation callback for confirmation pulse
static void anim_pulse_cb(void* var, int32_t value) {
    lv_obj_set_style_arc_width((lv_obj_t*)var, value, LV_PART_INDICATOR);
}

void ui_animate_confirm() {
    // Pulse animation on the arc
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_temp);
    lv_anim_set_values(&a, 20, 30);
    lv_anim_set_time(&a, 150);
    lv_anim_set_playback_time(&a, 150);
    lv_anim_set_exec_cb(&a, anim_pulse_cb);
    lv_anim_start(&a);

    // Flash status green briefly
    lv_label_set_text(label_status, "Temperature set!");
    lv_obj_set_style_text_color(label_status, COLOR_SUCCESS, 0);
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 70);
}
