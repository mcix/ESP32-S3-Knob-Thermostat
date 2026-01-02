#include "encoder.h"
#include "../config.h"
#include <Arduino.h>
#include <lvgl.h>

// Encoder state
static volatile int32_t encoder_position = 0;
static volatile int8_t last_direction = 0;
static volatile bool button_state = false;

// Callbacks
static encoder_callback_t rotation_callback = nullptr;
static button_callback_t button_callback = nullptr;

// LVGL input device
static lv_indev_drv_t enc_drv;
static lv_indev_t* enc_indev = nullptr;
static lv_group_t* enc_group = nullptr;

// Debounce settings
#define BUTTON_DEBOUNCE_MS 50

// Last encoder pin states
static volatile uint8_t last_state = 0;

// Encoder state tracking for full-step detection
static volatile int8_t enc_val = 0;

// Debug: store recent state transitions for logging
#define DEBUG_LOG_SIZE 16
static volatile uint8_t debug_states[DEBUG_LOG_SIZE];
static volatile int8_t debug_dirs[DEBUG_LOG_SIZE];
static volatile uint8_t debug_idx = 0;

// ISR for encoder rotation
// Only count transitions FROM resting state (11)
// CW: 11 -> 01, CCW: 11 -> 10
static void IRAM_ATTR encoder_isr() {
    uint8_t a = digitalRead(ENCODER_A);
    uint8_t b = digitalRead(ENCODER_B);
    uint8_t state = (a << 1) | b;

    // Ignore if state hasn't changed
    if (state == last_state) {
        return;
    }

    int8_t dir = 0;

    // Only count when leaving the 11 resting state
    if (last_state == 0b11) {
        if (state == 0b01) {
            // 11 -> 01: CW
            encoder_position++;
            dir = 1;
            last_direction = 1;
        } else if (state == 0b10) {
            // 11 -> 10: CCW
            encoder_position--;
            dir = -1;
            last_direction = -1;
        }
    }

    // Log state transition for debugging
    debug_states[debug_idx] = (last_state << 4) | state;
    debug_dirs[debug_idx] = dir;
    debug_idx = (debug_idx + 1) % DEBUG_LOG_SIZE;

    last_state = state;
}

// Print debug log of recent encoder transitions
void encoder_print_debug() {
    Serial.println("Encoder debug log (old_state -> new_state : direction):");
    for (int i = 0; i < DEBUG_LOG_SIZE; i++) {
        uint8_t idx = (debug_idx + i) % DEBUG_LOG_SIZE;
        uint8_t old_s = (debug_states[idx] >> 4) & 0x0F;
        uint8_t new_s = debug_states[idx] & 0x0F;
        int8_t dir = debug_dirs[idx];
        if (old_s != new_s || dir != 0) {
            Serial.printf("  %d%d -> %d%d : %s\n",
                          (old_s >> 1) & 1, old_s & 1,
                          (new_s >> 1) & 1, new_s & 1,
                          dir > 0 ? "CW" : (dir < 0 ? "CCW" : "-"));
        }
    }
}

// ISR for button press
static void IRAM_ATTR button_isr() {
    static uint32_t last_button_time = 0;
    uint32_t now = millis();

    if (now - last_button_time < BUTTON_DEBOUNCE_MS) {
        return;
    }
    last_button_time = now;

    button_state = !digitalRead(ENCODER_BTN);  // Active low
}

// LVGL encoder read callback
static void enc_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    static int32_t last_pos = 0;
    int32_t current_pos = encoder_position;

    int32_t diff = current_pos - last_pos;
    last_pos = current_pos;

    data->enc_diff = diff;
    data->state = button_state ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

bool encoder_init() {
    Serial.printf("Encoder init: A=GPIO%d, B=GPIO%d, BTN=GPIO%d\n", ENCODER_A, ENCODER_B, ENCODER_BTN);

    // Configure encoder pins with pull-up
    pinMode(ENCODER_A, INPUT_PULLUP);
    pinMode(ENCODER_B, INPUT_PULLUP);
    pinMode(ENCODER_BTN, INPUT_PULLUP);

    // Read initial state
    last_state = (digitalRead(ENCODER_A) << 1) | digitalRead(ENCODER_B);
    Serial.printf("Encoder initial state: A=%d, B=%d\n", digitalRead(ENCODER_A), digitalRead(ENCODER_B));

    // Attach interrupts
    attachInterrupt(digitalPinToInterrupt(ENCODER_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_B), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_BTN), button_isr, CHANGE);

    Serial.println("Encoder interrupts attached");
    return true;
}

void encoder_set_rotation_callback(encoder_callback_t callback) {
    rotation_callback = callback;
}

void encoder_set_button_callback(button_callback_t callback) {
    button_callback = callback;
}

int32_t encoder_get_position() {
    return encoder_position;
}

void encoder_reset_position() {
    noInterrupts();
    encoder_position = 0;
    interrupts();
}

bool encoder_button_pressed() {
    return button_state;
}

void encoder_update() {
    static int32_t prev_position = 0;
    static bool prev_button = false;
    static int8_t prev_dir = 0;
    static uint32_t last_rotation_time = 0;

    // Check for rotation change
    int32_t pos = encoder_position;
    if (pos != prev_position && rotation_callback) {
        int8_t dir = (pos > prev_position) ? 1 : -1;
        uint32_t now = millis();

        // Filter rapid direction changes (debounce direction reversals)
        // If direction reversed within 100ms, likely bounce - ignore
        if (dir != prev_dir && prev_dir != 0 && (now - last_rotation_time < 100)) {
            Serial.printf("Encoder: filtered spurious %s (too fast after %s)\n",
                          dir > 0 ? "CW" : "CCW", prev_dir > 0 ? "CW" : "CCW");
            // Reset position to ignore this spurious count
            noInterrupts();
            encoder_position = prev_position;
            interrupts();
            return;
        }

        // Valid rotation - process it
        rotation_callback(dir);
        prev_position = pos;
        prev_dir = dir;
        last_rotation_time = now;
    }

    // Check for button change
    bool btn = button_state;
    if (btn != prev_button && button_callback) {
        button_callback(btn);
        prev_button = btn;
    }
}

void encoder_register_lvgl() {
    // Create LVGL input device for encoder
    lv_indev_drv_init(&enc_drv);
    enc_drv.type = LV_INDEV_TYPE_ENCODER;
    enc_drv.read_cb = enc_read_cb;
    enc_indev = lv_indev_drv_register(&enc_drv);

    // Create a group for encoder navigation
    enc_group = lv_group_create();
    lv_indev_set_group(enc_indev, enc_group);
    lv_group_set_default(enc_group);

    Serial.println("Encoder registered with LVGL");
}
