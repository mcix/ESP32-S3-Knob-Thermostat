#ifndef CONFIG_H
#define CONFIG_H

// Include secrets (WiFi credentials, API keys, device IDs)
// Copy secrets.h.example to secrets.h and fill in your values
#include "secrets.h"

// ============================================
// Thermostat Settings
// ============================================
#define TEMP_MIN           0.0f     // Minimum temperature (Celsius)
#define TEMP_MAX           30.0f    // Maximum temperature (Celsius)
#define TEMP_STEP          0.5f     // Temperature adjustment step
#define TEMP_DEFAULT       20.0f    // Default temperature on startup

// ============================================
// Timing Configuration
// ============================================
#define THERMOSTAT_TIMEOUT_MS  60000   // Auto-switch to clock after inactivity
#define DEBOUNCE_DELAY_MS      2000    // Delay before sending temp change to Homey
#define REFRESH_INTERVAL_MS    30000   // Interval to refresh current temperature
#define WIFI_TIMEOUT_MS        10000   // WiFi connection timeout
#define WIFI_CHECK_INTERVAL_MS 2000    // How often to check WiFi link status
#define WIFI_RECONNECT_INTERVAL_MS 10000  // Delay between reconnect attempts
#define WIFI_REBOOT_AFTER_MS   300000  // Reboot if WiFi stays down this long (last resort)
#define API_TIMEOUT_MS         5000    // HTTP request timeout

// ============================================
// Display Configuration (ESP32-S3-Knob-Touch-LCD-1.8)
// Uses ST77916 display with QSPI interface
// ============================================
#define DISPLAY_WIDTH      360
#define DISPLAY_HEIGHT     360

// QSPI Pins for LCD (ST77916)
#define LCD_CS             14
#define LCD_SCLK           13
#define LCD_DATA0          15
#define LCD_DATA1          16
#define LCD_DATA2          17
#define LCD_DATA3          18
#define LCD_RST            21
#define LCD_BL             47

// I2C Pins for Touch (CST816)
// Based on ESPHome config: https://devices.esphome.io/devices/esp32s3-1.8-inch-jc3636k518c/
#define TOUCH_SDA          11
#define TOUCH_SCL          12
#define TOUCH_INT          9
#define TOUCH_RST          10

// ============================================
// Encoder Configuration
// Based on Waveshare ESP32-S3-Knob-Touch-LCD-1.8 pinout
// See: https://github.com/orgs/esphome/discussions/3253
// ============================================
#define ENCODER_A          8    // Rotary encoder pin A
#define ENCODER_B          7    // Rotary encoder pin B
#define ENCODER_BTN        0    // Encoder button pin (active low) - may be touch-based

// ============================================
// Haptic Feedback (DRV2605)
// ============================================
#define HAPTIC_SDA         11   // Shared I2C bus with touch
#define HAPTIC_SCL         12
#define HAPTIC_ENABLED     true

#endif // CONFIG_H
