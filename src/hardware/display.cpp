#include "display.h"
#include "../config.h"

#include <Arduino.h>
#include <lvgl.h>

// Arduino_GFX library
#include <Arduino_GFX_Library.h>
#include <display/Arduino_ST77916.h>

// LVGL objects
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_disp_t* disp = nullptr;

// Arduino_GFX objects
static Arduino_DataBus* bus = nullptr;
static Arduino_GFX* gfx = nullptr;

// Display buffer size - use 1/10 of screen height
#define DISP_BUF_HEIGHT (DISPLAY_HEIGHT / 10)

// LVGL display flush callback
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_p, w, h);

    lv_disp_flush_ready(drv);
}

// Touch input read callback (placeholder)
static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    data->state = LV_INDEV_STATE_REL;
}

bool display_init() {
    Serial.println("Initializing display with Arduino_GFX ST77916...");

    // Set backlight on first
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    Serial.println("Backlight on");

    // Create QSPI bus for ST77916
    // Arduino_ESP32QSPI(cs, sck, d0, d1, d2, d3)
    bus = new Arduino_ESP32QSPI(
        LCD_CS,     // CS
        LCD_SCLK,   // SCK
        LCD_DATA0,  // D0
        LCD_DATA1,  // D1
        LCD_DATA2,  // D2
        LCD_DATA3   // D3
    );

    // Create ST77916 display with 150-degree init operations
    gfx = new Arduino_ST77916(
        bus,
        LCD_RST,    // RST pin
        0,          // rotation (0 = normal)
        true,       // IPS mode
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        0,          // col_offset1
        0,          // row_offset1
        0,          // col_offset2
        0,          // row_offset2
        st77916_150_init_operations,
        sizeof(st77916_150_init_operations)
    );

    // Initialize the display
    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
        return false;
    }
    Serial.println("Arduino_GFX initialized");

    // Don't invert display colors - LVGL handles colors correctly
    gfx->invertDisplay(false);

    // Fill screen with black to verify display is working
    gfx->fillScreen(0x0000);
    Serial.println("Screen cleared");

    // Initialize LVGL
    lv_init();
    Serial.println("LVGL initialized");

    // Allocate display buffers using DMA-capable memory
    size_t buf_size = DISPLAY_WIDTH * DISP_BUF_HEIGHT * sizeof(lv_color_t);
    Serial.printf("Allocating display buffers: %d bytes each\n", buf_size);

    buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);

    if (!buf1) {
        Serial.println("Failed to allocate display buffer 1!");
        return false;
    }

    if (buf2) {
        lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISPLAY_WIDTH * DISP_BUF_HEIGHT);
        Serial.println("Double buffering enabled");
    } else {
        lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISPLAY_WIDTH * DISP_BUF_HEIGHT);
        Serial.println("Single buffering (buf2 allocation failed)");
    }

    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp = lv_disp_drv_register(&disp_drv);

    // Initialize touch input driver (placeholder)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    Serial.println("Display initialization complete!");
    return true;
}

void display_update() {
    lv_timer_handler();
}

void display_set_brightness(uint8_t brightness) {
    analogWrite(LCD_BL, brightness);
}

lv_disp_t* display_get() {
    return disp;
}
