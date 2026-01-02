#include "touch.h"
#include "../config.h"
#include <Arduino.h>
#include <Wire.h>

// CST816 I2C address
#define CST816_ADDR 0x15

// CST816 registers
#define CST816_REG_GESTURE   0x01
#define CST816_REG_FINGER    0x02
#define CST816_REG_XH        0x03
#define CST816_REG_XL        0x04
#define CST816_REG_YH        0x05
#define CST816_REG_YL        0x06

// Touch state tracking
static bool last_touch_state = false;
static bool tap_detected = false;
static uint32_t touch_start_time = 0;

bool touch_init() {
    Serial.printf("Touch init: SDA=GPIO%d, SCL=GPIO%d, INT=GPIO%d, RST=GPIO%d\n",
                  TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST);

    // Reset touch controller
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(50);

    // Initialize I2C
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);  // 400kHz

    // Setup interrupt pin
    pinMode(TOUCH_INT, INPUT);

    // Check if touch controller responds
    Wire.beginTransmission(CST816_ADDR);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
        Serial.println("CST816 touch controller found");
        return true;
    } else {
        Serial.printf("CST816 not found, I2C error: %d\n", error);
        return false;
    }
}

bool touch_read(uint16_t* x, uint16_t* y) {
    uint8_t data[6];

    // Read touch data starting from register 0x01
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(CST816_REG_GESTURE);
    Wire.endTransmission(false);

    Wire.requestFrom(CST816_ADDR, 6);
    if (Wire.available() < 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    // data[1] = finger count (0 = no touch, 1 = touching)
    uint8_t finger_num = data[1];

    if (finger_num == 0) {
        return false;
    }

    // Extract coordinates
    // data[2] = XH (high 4 bits of X in lower nibble)
    // data[3] = XL (low 8 bits of X)
    // data[4] = YH (high 4 bits of Y in lower nibble)
    // data[5] = YL (low 8 bits of Y)
    *x = ((data[2] & 0x0F) << 8) | data[3];
    *y = ((data[4] & 0x0F) << 8) | data[5];

    return true;
}

bool touch_tapped() {
    uint16_t x, y;
    bool is_touching = touch_read(&x, &y);

    // Detect tap: touch was active, now released, and duration < 500ms
    if (last_touch_state && !is_touching) {
        uint32_t duration = millis() - touch_start_time;
        if (duration < 500) {
            tap_detected = true;
        }
    }

    // Track touch start time
    if (!last_touch_state && is_touching) {
        touch_start_time = millis();
    }

    last_touch_state = is_touching;

    // Return and clear tap flag
    if (tap_detected) {
        tap_detected = false;
        return true;
    }
    return false;
}
