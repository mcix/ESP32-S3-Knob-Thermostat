#include "ui.h"
#include "weather_icons.h"
#include "../config.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// UI Elements
static lv_obj_t* screen = nullptr;
static lv_obj_t* thermostat_container = nullptr;  // Container for all thermostat elements
static lv_obj_t* bg_circle = nullptr;
static lv_obj_t* label_target = nullptr;
static lv_obj_t* label_current = nullptr;
static lv_obj_t* label_status = nullptr;  // Small status text below current temp

// Tick marks
#define NUM_TICKS 60
#define TICK_ARC_DEGREES 270.0f
#define TICK_START_ANGLE 135.0f

static lv_obj_t* ticks[NUM_TICKS];
static lv_point_t tick_points[NUM_TICKS][2];
static lv_point_t tick_points_long[NUM_TICKS][2];

// Outside weather elements (Lucide icon rendered as a tintable LVGL image)
static lv_obj_t* weather_container = nullptr;
static lv_obj_t* label_weather = nullptr;
static lv_obj_t* weather_icon = nullptr;

// Current state
static float current_target = TEMP_DEFAULT;
static float current_temp = TEMP_DEFAULT;
static bool current_heating = false;
static UIState current_state = UI_STATE_CONNECTING;

// Status clear timer
static uint32_t status_clear_time = 0;

// Colors
static const lv_color_t COLOR_HEATING = lv_color_hex(0xE46A2C);     // RGB 228, 106, 44
static const lv_color_t COLOR_IDLE = lv_color_hex(0x000000);
static const lv_color_t COLOR_TEXT = lv_color_hex(0xFFFFFF);
static const lv_color_t COLOR_TICK_ACTIVE = lv_color_hex(0xFFFFFF);
static const lv_color_t COLOR_SUN = lv_color_hex(0xFFC83C);
static const lv_color_t COLOR_CLOUD = lv_color_hex(0xC8C8C8);
static const lv_color_t COLOR_RAIN = lv_color_hex(0x4FA8FF);
static const lv_color_t COLOR_SNOW = lv_color_hex(0xFFFFFF);

// Convert temperature to tick index (0-59)
static int temp_to_tick_index(float temp) {
    float normalized = (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    normalized = fmax(0.0f, fmin(1.0f, normalized));
    return (int)(normalized * (NUM_TICKS - 1));
}

// Update tick mark styles based on current and target temperature
static void update_tick_styles() {
    int current_idx = temp_to_tick_index(current_temp);
    int target_idx = temp_to_tick_index(current_target);

    int low_idx = min(current_idx, target_idx);
    int high_idx = max(current_idx, target_idx);

    for (int i = 0; i < NUM_TICKS; i++) {
        if (ticks[i] == nullptr) continue;

        bool is_current = (i == current_idx);
        bool is_target = (i == target_idx);
        bool is_between = (i >= low_idx && i <= high_idx);

        lv_opa_t opacity = is_between ? LV_OPA_100 : LV_OPA_70;
        lv_obj_set_style_line_opa(ticks[i], opacity, 0);

        int width = 2;
        if (is_current || is_target) {
            width = 4;
        }
        lv_obj_set_style_line_width(ticks[i], width, 0);

        if (is_target) {
            lv_line_set_points(ticks[i], tick_points_long[i], 2);
        } else {
            lv_line_set_points(ticks[i], tick_points[i], 2);
        }

        lv_obj_set_style_line_color(ticks[i], COLOR_TICK_ACTIVE, 0);
    }
}

// Create tick marks around the edge (top 75% of circle)
static void create_tick_marks() {
    int cx = DISPLAY_WIDTH / 2;
    int cy = DISPLAY_HEIGHT / 2;
    int outer_r = 175;
    int inner_r = 160;
    int inner_r_long = 150;

    for (int i = 0; i < NUM_TICKS; i++) {
        float angle = TICK_START_ANGLE + (float)i * (TICK_ARC_DEGREES / (NUM_TICKS - 1));
        float rad = angle * M_PI / 180.0f;

        int x1 = cx + (int)(inner_r * cosf(rad));
        int y1 = cy + (int)(inner_r * sinf(rad));
        int x2 = cx + (int)(outer_r * cosf(rad));
        int y2 = cy + (int)(outer_r * sinf(rad));

        tick_points[i][0].x = x1;
        tick_points[i][0].y = y1;
        tick_points[i][1].x = x2;
        tick_points[i][1].y = y2;

        int x1_long = cx + (int)(inner_r_long * cosf(rad));
        int y1_long = cy + (int)(inner_r_long * sinf(rad));

        tick_points_long[i][0].x = x1_long;
        tick_points_long[i][0].y = y1_long;
        tick_points_long[i][1].x = x2;
        tick_points_long[i][1].y = y2;

        ticks[i] = lv_line_create(thermostat_container);
        lv_line_set_points(ticks[i], tick_points[i], 2);
        lv_obj_set_style_line_color(ticks[i], COLOR_TICK_ACTIVE, 0);
        lv_obj_set_style_line_width(ticks[i], 2, 0);
        lv_obj_set_style_line_rounded(ticks[i], true, 0);
        lv_obj_set_style_line_opa(ticks[i], LV_OPA_70, 0);
    }
}

// Build the outside-weather readout: a Lucide condition icon (tinted at
// draw time) plus the outside temperature label, above the target temp
static void create_weather_elements() {
    weather_container = lv_obj_create(thermostat_container);
    lv_obj_remove_style_all(weather_container);
    lv_obj_set_size(weather_container, 108, 44);
    lv_obj_align(weather_container, LV_ALIGN_CENTER, 0, -90);
    lv_obj_clear_flag(weather_container, LV_OBJ_FLAG_SCROLLABLE);

    // Condition icon: an alpha-8bit image recolored per condition
    weather_icon = lv_img_create(weather_container);
    lv_img_set_src(weather_icon, &wi_sun);
    lv_obj_align(weather_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_img_recolor_opa(weather_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(weather_icon, COLOR_SUN, 0);

    // Outside temperature label next to the icon
    label_weather = lv_label_create(weather_container);
    lv_obj_set_style_text_font(label_weather, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label_weather, COLOR_TEXT, 0);
    lv_label_set_text(label_weather, "--.-°");
    lv_obj_align(label_weather, LV_ALIGN_LEFT_MID, 52, 0);

    // Hidden until the first weather fetch succeeds
    lv_obj_add_flag(weather_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_init() {
    screen = lv_scr_act();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, COLOR_IDLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    // Create thermostat container
    thermostat_container = lv_obj_create(screen);
    lv_obj_remove_style_all(thermostat_container);
    lv_obj_set_size(thermostat_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(thermostat_container, 0, 0);
    lv_obj_set_style_bg_opa(thermostat_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(thermostat_container, LV_OBJ_FLAG_SCROLLABLE);

    // Background circle
    bg_circle = lv_obj_create(thermostat_container);
    lv_obj_remove_style_all(bg_circle);
    lv_obj_set_size(bg_circle, 350, 350);
    lv_obj_center(bg_circle);
    lv_obj_set_style_radius(bg_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bg_circle, COLOR_IDLE, 0);
    lv_obj_set_style_bg_opa(bg_circle, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bg_circle, LV_OBJ_FLAG_SCROLLABLE);

    // Tick marks
    create_tick_marks();

    // Target temperature (large, center)
    label_target = lv_label_create(thermostat_container);
    lv_obj_set_style_text_font(label_target, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label_target, COLOR_TEXT, 0);
    lv_label_set_text(label_target, "--.-°");
    lv_obj_align(label_target, LV_ALIGN_CENTER, 0, -20);

    // Current temperature (below target)
    label_current = lv_label_create(thermostat_container);
    lv_obj_set_style_text_font(label_current, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(label_current, COLOR_TEXT, 0);
    lv_label_set_text(label_current, "--.-°");
    lv_obj_align(label_current, LV_ALIGN_CENTER, 0, 35);

    // Small status text (12px, below current temp)
    label_status = lv_label_create(thermostat_container);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_status, COLOR_TEXT, 0);
    lv_obj_set_style_text_opa(label_status, LV_OPA_70, 0);
    lv_label_set_text(label_status, "");
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 75);

    // Outside weather readout (above the target temperature)
    create_weather_elements();

    Serial.println("UI initialized");
}

void ui_set_weather(float temp, int wmo_code) {
    if (weather_container == nullptr) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", temp);
    lv_label_set_text(label_weather, buf);

    // Map the WMO weather interpretation code to a Lucide icon and tint.
    // Codes: 0-1 clear, 2 partly cloudy, 3 overcast, 45/48 fog,
    // 51-57 drizzle, 61-67 rain, 71-77 snow, 80-82 rain showers,
    // 85/86 snow showers, 95-99 thunderstorm.
    const lv_img_dsc_t* src;
    lv_color_t tint;

    if (wmo_code <= 1) {
        src = &wi_sun;             tint = COLOR_SUN;    // Clear / mainly clear
    } else if (wmo_code == 2) {
        src = &wi_cloud_sun;       tint = COLOR_SUN;    // Partly cloudy
    } else if (wmo_code == 45 || wmo_code == 48) {
        src = &wi_cloud_fog;       tint = COLOR_CLOUD;  // Fog
    } else if ((wmo_code >= 71 && wmo_code <= 77) || wmo_code == 85 || wmo_code == 86) {
        src = &wi_cloud_snow;      tint = COLOR_SNOW;   // Snow
    } else if (wmo_code >= 95) {
        src = &wi_cloud_lightning; tint = COLOR_SUN;    // Thunderstorm
    } else if (wmo_code >= 80) {
        src = &wi_cloud_rain;      tint = COLOR_RAIN;   // Rain showers
    } else if (wmo_code >= 51) {
        src = &wi_cloud_drizzle;   tint = COLOR_RAIN;   // Drizzle / rain
    } else {
        src = &wi_cloudy;          tint = COLOR_CLOUD;  // Overcast and anything else
    }

    lv_img_set_src(weather_icon, src);
    lv_obj_set_style_img_recolor(weather_icon, tint, 0);
    lv_obj_align(weather_icon, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_clear_flag(weather_container, LV_OBJ_FLAG_HIDDEN);
}

void ui_set_target_temp(float temp) {
    current_target = temp;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", temp);
    lv_label_set_text(label_target, buf);
    lv_obj_align(label_target, LV_ALIGN_CENTER, 0, -20);

    update_tick_styles();
}

void ui_set_current_temp(float temp) {
    current_temp = temp;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", temp);
    lv_label_set_text(label_current, buf);
    lv_obj_align(label_current, LV_ALIGN_CENTER, 0, 35);

    update_tick_styles();
}

void ui_set_heating(bool heating) {
    if (current_heating == heating) return;
    current_heating = heating;

    lv_color_t bg_color = heating ? COLOR_HEATING : COLOR_IDLE;
    lv_obj_set_style_bg_color(screen, bg_color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bg_circle, bg_color, 0);

    Serial.printf("UI: Heating %s\n", heating ? "ON" : "OFF");
}

void ui_update_arc(float temp) {
    // Not used
}

void ui_set_state(UIState state) {
    current_state = state;
    // States are now handled via ui_show_status
}

void ui_show_error(const char* message) {
    lv_label_set_text(label_status, message);
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 75);
}

void ui_clear_error() {
    lv_label_set_text(label_status, "");
}

lv_obj_t* ui_get_arc() {
    return nullptr;
}

void ui_animate_confirm() {
    // Not used anymore
}

// New functions for status text
void ui_show_status(const char* text) {
    lv_label_set_text(label_status, text);
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 75);
}

void ui_clear_status() {
    lv_label_set_text(label_status, "");
}

void ui_set_status_clear_timer(uint32_t delay_ms) {
    status_clear_time = millis() + delay_ms;
}

void ui_update() {
    // Check if status should be cleared
    if (status_clear_time > 0 && millis() >= status_clear_time) {
        ui_clear_status();
        status_clear_time = 0;
    }
}

void ui_set_visible(bool visible) {
    if (thermostat_container == nullptr) return;

    if (visible) {
        lv_obj_clear_flag(thermostat_container, LV_OBJ_FLAG_HIDDEN);
        // Update background color based on heating state
        lv_color_t bg_color = current_heating ? COLOR_HEATING : COLOR_IDLE;
        lv_obj_set_style_bg_color(screen, bg_color, LV_PART_MAIN);
    } else {
        lv_obj_add_flag(thermostat_container, LV_OBJ_FLAG_HIDDEN);
        // Reset background to black when not showing thermostat
        lv_obj_set_style_bg_color(screen, COLOR_IDLE, LV_PART_MAIN);
    }
}

lv_obj_t* ui_get_container() {
    return thermostat_container;
}
