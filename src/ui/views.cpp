#include "views.h"
#include "ui.h"
#include "weather_icon_map.h"
#include "../config.h"
#include <Arduino.h>
#include <math.h>
#include <time.h>

// View containers
static lv_obj_t* view_clock = nullptr;
static lv_obj_t* view_thermostat = nullptr;

// Current view
static ViewType current_view = VIEW_CLOCK;

// Inactivity timeout - auto-switch back to clock
static uint32_t last_activity_time = 0;

// Clock elements
static lv_obj_t* clock_face = nullptr;
static lv_obj_t* hour_hand = nullptr;
static lv_obj_t* minute_hand = nullptr;
static lv_obj_t* second_hand = nullptr;
static lv_obj_t* center_dot = nullptr;
static lv_obj_t* time_label = nullptr;

// Indoor/outdoor temperature readout above the clock center
static lv_obj_t* clock_indoor_label = nullptr;
static lv_obj_t* clock_outdoor_icon = nullptr;
static lv_obj_t* clock_outdoor_label = nullptr;

// Clock hand line points
static lv_point_t hour_points[2];
static lv_point_t minute_points[2];
static lv_point_t second_points[2];

// Colors
static const lv_color_t COLOR_BG = lv_color_hex(0x000000);
static const lv_color_t COLOR_CLOCK_FACE = lv_color_hex(0x000000);
static const lv_color_t COLOR_TICK = lv_color_hex(0xFFFFFF);
static const lv_color_t COLOR_HOUR = lv_color_hex(0xFFFFFF);
static const lv_color_t COLOR_MINUTE = lv_color_hex(0xFFFFFF);
static const lv_color_t COLOR_SECOND = lv_color_hex(0xE46A2C);  // Orange accent
static const lv_color_t COLOR_CENTER = lv_color_hex(0xFFFFFF);

// Clock dimensions — scaled to fill the 360x360 screen (outer tick at
// r=176, 4px from the edge). Hand lengths and tick offsets keep the same
// proportions to CLOCK_RADIUS as the original 160px design.
#define CLOCK_RADIUS 188
#define HOUR_HAND_LEN (CLOCK_RADIUS / 2)        // 94  (was 80 @ R=160)
#define MINUTE_HAND_LEN (CLOCK_RADIUS * 3 / 4)  // 141 (was 120)
#define SECOND_HAND_LEN (CLOCK_RADIUS * 13 / 16) // 152 (was 130)
#define CENTER_X (DISPLAY_WIDTH / 2)
#define CENTER_Y (DISPLAY_HEIGHT / 2)

// Forward declarations
static void create_clock_view();
static void update_clock_hands();

void views_init() {
    // Create clock view
    create_clock_view();

    // Thermostat view is created by ui_init() - we'll get reference to it
    // For now, thermostat UI elements are on the active screen directly

    // Start with clock view
    views_switch(VIEW_CLOCK);
}

static void create_clock_view() {
    // Create clock container
    view_clock = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(view_clock);
    lv_obj_set_size(view_clock, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(view_clock, 0, 0);
    lv_obj_set_style_bg_color(view_clock, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(view_clock, LV_OPA_COVER, 0);
    lv_obj_clear_flag(view_clock, LV_OBJ_FLAG_SCROLLABLE);

    // Create clock face background circle
    clock_face = lv_obj_create(view_clock);
    lv_obj_remove_style_all(clock_face);
    // Background circle: full-screen (invisible, black on black). Capped at
    // the display size so it never exceeds the screen as CLOCK_RADIUS grows.
    lv_obj_set_size(clock_face, min(CLOCK_RADIUS * 2, DISPLAY_WIDTH),
                    min(CLOCK_RADIUS * 2, DISPLAY_HEIGHT));
    lv_obj_center(clock_face);
    lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(clock_face, COLOR_CLOCK_FACE, 0);
    lv_obj_set_style_bg_opa(clock_face, LV_OPA_COVER, 0);
    lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);

    // Create hour tick marks
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * M_PI / 180.0f;  // -90 to start at 12 o'clock
        int outer_r = CLOCK_RADIUS - 12;
        int inner_r = (i % 3 == 0) ? CLOCK_RADIUS - 35 : CLOCK_RADIUS - 24;  // Longer at 12, 3, 6, 9

        static lv_point_t tick_points[12][2];
        tick_points[i][0].x = CENTER_X + (int)(inner_r * cosf(angle));
        tick_points[i][0].y = CENTER_Y + (int)(inner_r * sinf(angle));
        tick_points[i][1].x = CENTER_X + (int)(outer_r * cosf(angle));
        tick_points[i][1].y = CENTER_Y + (int)(outer_r * sinf(angle));

        lv_obj_t* tick = lv_line_create(view_clock);
        lv_line_set_points(tick, tick_points[i], 2);
        lv_obj_set_style_line_color(tick, COLOR_TICK, 0);
        lv_obj_set_style_line_width(tick, (i % 3 == 0) ? 4 : 2, 0);
        lv_obj_set_style_line_rounded(tick, true, 0);
    }

    // Create hour hand
    hour_hand = lv_line_create(view_clock);
    lv_obj_set_style_line_color(hour_hand, COLOR_HOUR, 0);
    lv_obj_set_style_line_width(hour_hand, 6, 0);
    lv_obj_set_style_line_rounded(hour_hand, true, 0);

    // Create minute hand
    minute_hand = lv_line_create(view_clock);
    lv_obj_set_style_line_color(minute_hand, COLOR_MINUTE, 0);
    lv_obj_set_style_line_width(minute_hand, 4, 0);
    lv_obj_set_style_line_rounded(minute_hand, true, 0);

    // Create second hand
    second_hand = lv_line_create(view_clock);
    lv_obj_set_style_line_color(second_hand, COLOR_SECOND, 0);
    lv_obj_set_style_line_width(second_hand, 2, 0);
    lv_obj_set_style_line_rounded(second_hand, true, 0);

    // Create center dot
    center_dot = lv_obj_create(view_clock);
    lv_obj_remove_style_all(center_dot);
    lv_obj_set_size(center_dot, 14, 14);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, COLOR_CENTER, 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_align(center_dot, LV_ALIGN_CENTER, 0, 0);

    // Digital time, in the lower half between the center and the bottom tick
    time_label = lv_label_create(view_clock);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(time_label, COLOR_TICK, 0);
    lv_label_set_text(time_label, "00:00");
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 64);

    // Outdoor temperature (weather icon + value) on top, above the center
    lv_obj_t* out_row = lv_obj_create(view_clock);
    lv_obj_remove_style_all(out_row);
    lv_obj_set_size(out_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(out_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(out_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(out_row, 5, 0);
    lv_obj_clear_flag(out_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(out_row, LV_ALIGN_CENTER, 0, -80);

    // 24px condition icon (native size, ~matches the temp text height)
    clock_outdoor_icon = lv_img_create(out_row);
    lv_img_set_src(clock_outdoor_icon, &wi_sun_s);
    lv_obj_set_style_img_recolor_opa(clock_outdoor_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(clock_outdoor_icon, lv_color_hex(0xFFC83C), 0);

    clock_outdoor_label = lv_label_create(out_row);
    lv_obj_set_style_text_font(clock_outdoor_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(clock_outdoor_label, COLOR_TICK, 0);
    lv_label_set_text(clock_outdoor_label, "--.-°");

    // Indoor temperature (home glyph + value) just below the outdoor row
    clock_indoor_label = lv_label_create(view_clock);
    lv_obj_set_style_text_font(clock_indoor_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(clock_indoor_label, COLOR_TICK, 0);
    lv_label_set_text(clock_indoor_label, LV_SYMBOL_HOME "  --.-°");
    lv_obj_align(clock_indoor_label, LV_ALIGN_CENTER, 0, -48);

    // Keep the clock hands and center dot on top of the temperature/time
    // labels (z-order follows creation order, so raise them last)
    lv_obj_move_foreground(hour_hand);
    lv_obj_move_foreground(minute_hand);
    lv_obj_move_foreground(second_hand);
    lv_obj_move_foreground(center_dot);

    // Initial hand positions
    update_clock_hands();
}

static void update_clock_hands() {
    // Get current time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hours = timeinfo.tm_hour % 12;
    int minutes = timeinfo.tm_min;
    int seconds = timeinfo.tm_sec;

    // Calculate angles (0 degrees = 12 o'clock, clockwise)
    float hour_angle = ((hours * 60 + minutes) / 720.0f) * 360.0f - 90.0f;
    float minute_angle = (minutes / 60.0f) * 360.0f - 90.0f;
    float second_angle = (seconds / 60.0f) * 360.0f - 90.0f;

    // Convert to radians
    float hour_rad = hour_angle * M_PI / 180.0f;
    float minute_rad = minute_angle * M_PI / 180.0f;
    float second_rad = second_angle * M_PI / 180.0f;

    // Update hour hand
    hour_points[0].x = CENTER_X;
    hour_points[0].y = CENTER_Y;
    hour_points[1].x = CENTER_X + (int)(HOUR_HAND_LEN * cosf(hour_rad));
    hour_points[1].y = CENTER_Y + (int)(HOUR_HAND_LEN * sinf(hour_rad));
    lv_line_set_points(hour_hand, hour_points, 2);

    // Update minute hand
    minute_points[0].x = CENTER_X;
    minute_points[0].y = CENTER_Y;
    minute_points[1].x = CENTER_X + (int)(MINUTE_HAND_LEN * cosf(minute_rad));
    minute_points[1].y = CENTER_Y + (int)(MINUTE_HAND_LEN * sinf(minute_rad));
    lv_line_set_points(minute_hand, minute_points, 2);

    // Update second hand
    second_points[0].x = CENTER_X;
    second_points[0].y = CENTER_Y;
    second_points[1].x = CENTER_X + (int)(SECOND_HAND_LEN * cosf(second_rad));
    second_points[1].y = CENTER_Y + (int)(SECOND_HAND_LEN * sinf(second_rad));
    lv_line_set_points(second_hand, second_points, 2);

    // Update digital time
    char time_str[6];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text(time_label, time_str);
}

void views_switch(ViewType view) {
    current_view = view;

    // Hide all views first
    if (view_clock) lv_obj_add_flag(view_clock, LV_OBJ_FLAG_HIDDEN);

    // Show selected view
    switch (view) {
        case VIEW_CLOCK:
            if (view_clock) {
                lv_obj_clear_flag(view_clock, LV_OBJ_FLAG_HIDDEN);
                update_clock_hands();
            }
            // Hide thermostat elements
            ui_set_visible(false);
            Serial.println("Switched to clock view");
            break;

        case VIEW_THERMOSTAT:
            // Show thermostat elements
            ui_set_visible(true);
            last_activity_time = millis();
            Serial.println("Switched to thermostat view");
            break;

        default:
            break;
    }
}

void views_next() {
    ViewType next = (ViewType)((current_view + 1) % VIEW_COUNT);
    views_switch(next);
}

ViewType views_current() {
    return current_view;
}

void views_reset_activity() {
    last_activity_time = millis();
}

void views_set_indoor_temp(float temp) {
    if (clock_indoor_label == nullptr) return;
    char buf[24];
    snprintf(buf, sizeof(buf), LV_SYMBOL_HOME "  %.1f°", temp);
    lv_label_set_text(clock_indoor_label, buf);
    lv_obj_align(clock_indoor_label, LV_ALIGN_CENTER, 0, -48);
}

void views_set_outdoor(float temp, int wmo_code) {
    if (clock_outdoor_label == nullptr) return;

    lv_color_t tint;
    const lv_img_dsc_t* src = weather_icon_small_for_code(wmo_code, &tint);
    lv_img_set_src(clock_outdoor_icon, src);
    lv_obj_set_style_img_recolor(clock_outdoor_icon, tint, 0);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", temp);
    lv_label_set_text(clock_outdoor_label, buf);
}

void views_update() {
    static uint32_t last_clock_update = 0;
    uint32_t now = millis();

    // Auto-switch back to clock after inactivity on thermostat view
    if (current_view == VIEW_THERMOSTAT && (now - last_activity_time >= THERMOSTAT_TIMEOUT_MS)) {
        Serial.println("Thermostat inactive - switching to clock");
        views_switch(VIEW_CLOCK);
    }

    // Update clock every second when visible
    if (current_view == VIEW_CLOCK && (now - last_clock_update >= 1000)) {
        last_clock_update = now;
        update_clock_hands();
    }
}
