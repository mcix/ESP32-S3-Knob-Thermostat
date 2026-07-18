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

// Diagnostic logging of decode decisions (printed from main-loop context)
static const bool ENC_DIAG = false;

// This knob does not emit standard quadrature (verified with the PCNT
// peripheral: the A and B pulses never overlap, state 00 never occurs).
// Measured behavior per detent click:
//  - CW click:  one short low pulse on A, then a burst of 2-4 low pulses
//               on B belonging to the same click
//  - CCW click: a single low pulse on B, no A activity
// So direction is decoded from pulse timing:
//  - A pulse                          -> CW step
//  - B pulse shortly after CW activity -> tail of the CW click, ignored
//  - B pulse otherwise                -> CCW step (with bounce debounce)
#define CW_TAIL_SUPPRESS_MS 200  // B pulses this soon after CW activity are CW tail
#define A_BOUNCE_MS         15   // A pulses this close together are bounce
#define B_BOUNCE_MS         15   // B pulses this close together are bounce

// Pulse event queue (ISR producer, main loop consumer)
#define EVT_QUEUE_SIZE 32
static volatile uint32_t evt_time[EVT_QUEUE_SIZE];
static volatile uint8_t evt_code[EVT_QUEUE_SIZE];  // (from_state << 4) | to_state
static volatile uint8_t evt_widx = 0;

// Last encoder pin states
static volatile uint8_t last_state = 0;

// ISR for encoder pins: queue every state transition with a timestamp;
// decoding happens in the main loop where timing logic and logging are safe
static void IRAM_ATTR encoder_isr() {
    uint8_t a = digitalRead(ENCODER_A);
    uint8_t b = digitalRead(ENCODER_B);
    uint8_t state = (a << 1) | b;

    // Ignore if state hasn't changed
    if (state == last_state) {
        return;
    }

    evt_time[evt_widx] = millis();
    evt_code[evt_widx] = (last_state << 4) | state;
    evt_widx = (evt_widx + 1) % EVT_QUEUE_SIZE;

    last_state = state;
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

// Decode queued pulse events. Emits rotation callbacks when emit is true;
// otherwise events are consumed silently but timing state stays coherent.
// Returns the number of rotation steps decoded this call.
static int process_events(bool emit) {
    int steps = 0;
    static uint8_t ridx = 0;
    static uint32_t last_cw_ms = 0;   // Last A pulse or suppressed B (CW activity)
    static uint32_t last_a_ms = 0;
    static uint32_t last_b_ms = 0;

    while (ridx != evt_widx) {
        uint32_t t = evt_time[ridx];
        uint8_t code = evt_code[ridx];
        ridx = (ridx + 1) % EVT_QUEUE_SIZE;

        if (code == 0x31) {  // 11 -> 01: A fell
            last_cw_ms = t;
            if (t - last_a_ms < A_BOUNCE_MS) {
                if (ENC_DIAG) Serial.printf("[enc] t=%u A pulse (bounce, ignored)\n", (unsigned)t);
            } else {
                last_direction = 1;
                encoder_position++;
                steps++;
                if (ENC_DIAG) Serial.printf("[enc] t=%u A pulse -> CW\n", (unsigned)t);
                if (emit && rotation_callback) rotation_callback(1);
            }
            last_a_ms = t;
        } else if (code == 0x32) {  // 11 -> 10: B fell
            if (t - last_cw_ms < CW_TAIL_SUPPRESS_MS) {
                // Part of the ongoing CW click; keep the window open so a
                // long pulse burst stays suppressed as one click
                last_cw_ms = t;
                if (ENC_DIAG) Serial.printf("[enc] t=%u B pulse (CW tail, ignored)\n", (unsigned)t);
            } else if (t - last_b_ms < B_BOUNCE_MS) {
                if (ENC_DIAG) Serial.printf("[enc] t=%u B pulse (bounce, ignored)\n", (unsigned)t);
            } else {
                last_direction = -1;
                encoder_position--;
                steps++;
                if (ENC_DIAG) Serial.printf("[enc] t=%u B pulse -> CCW\n", (unsigned)t);
                if (emit && rotation_callback) rotation_callback(-1);
            }
            last_b_ms = t;
        } else if (ENC_DIAG) {
            Serial.printf("[enc] t=%u transition %d%d->%d%d\n", (unsigned)t,
                          (code >> 5) & 1, (code >> 4) & 1,
                          (code >> 1) & 1, code & 1);
        }
    }

    return steps;
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
    static bool prev_button = false;

    process_events(true);

    // Check for button change
    bool btn = button_state;
    if (btn != prev_button && button_callback) {
        button_callback(btn);
        prev_button = btn;
    }
}

int encoder_flush() {
    return process_events(false);
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
