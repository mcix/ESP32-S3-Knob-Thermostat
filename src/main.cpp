#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/encoder.h"
#include "hardware/touch.h"
#include "homey/homey_api.h"
#include "ui/ui.h"
#include "ui/views.h"

// Global instances
HomeyAPI homey;
ThermostatState thermostatState;

// Temperature control state
static float confirmedTemp = TEMP_DEFAULT;   // Last confirmed temp from Homey
static float pendingTemp = TEMP_DEFAULT;     // Temperature being adjusted
static uint32_t pendingTempTime = 0;         // When pendingTemp was last changed
static bool hasPendingTemp = false;          // True when there's an unsent change
static bool isPushingTemp = false;           // True while pushing to Homey
static bool waitingForConfirm = false;       // True after successful push, waiting for periodic refresh
static uint32_t lastRefreshTime = 0;

// Forward declarations
void onEncoderRotation(int8_t direction);
void onEncoderButton(bool pressed);
void connectWiFi();
void fetchThermostatState();
void checkAndPushTemperature();

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Thermostat Control ===");
    Serial.println("Initializing...");

    // Initialize display and LVGL
    if (!display_init()) {
        Serial.println("Display initialization failed!");
        while (1) delay(100);
    }

    // Set backlight to 25%
    display_set_brightness(128 / 2);

    // Initialize UI
    ui_init();
    ui_show_status("Connecting...");

    // Initialize encoder
    if (!encoder_init()) {
        Serial.println("Encoder initialization failed!");
    }
    encoder_set_rotation_callback(onEncoderRotation);
    encoder_set_button_callback(onEncoderButton);
    encoder_register_lvgl();

    // Initialize touch
    if (!touch_init()) {
        Serial.println("Touch initialization failed!");
    }

    // Initialize views (creates clock view)
    views_init();

    // Update display
    display_update();

    // Connect to WiFi
    connectWiFi();

    // Configure time for Amsterdam (CET/CEST with automatic DST)
    // POSIX timezone: CET-1CEST,M3.5.0/2,M10.5.0/3
    // - CET = standard time name, -1 = UTC+1
    // - CEST = daylight saving time name
    // - M3.5.0/2 = DST starts last Sunday of March at 02:00
    // - M10.5.0/3 = DST ends last Sunday of October at 03:00
    configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    Serial.println("Waiting for NTP time sync (Amsterdam timezone)...");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
        Serial.printf("Time: %02d:%02d:%02d (DST: %s)\n",
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                      timeinfo.tm_isdst ? "yes" : "no");
    } else {
        Serial.println("Failed to get time");
    }

    // Initialize Homey API
    homey.begin(HOMEY_IP, HOMEY_API_KEY, THERMOSTAT_DEVICE_ID);

    // Fetch initial thermostat state
    fetchThermostatState();

    Serial.println("Initialization complete!");
}

void loop() {
    static uint32_t lastLvglUpdate = 0;
    uint32_t now = millis();

    // Update LVGL
    if (now - lastLvglUpdate >= 5) {
        lastLvglUpdate = now;
        display_update();
        ui_update();
        views_update();
    }

    // Check for touch to toggle views
    if (touch_tapped()) {
        Serial.println("Touch detected - switching view");
        views_next();
    }

    // Process encoder callbacks (only affects thermostat view)
    if (views_current() == VIEW_THERMOSTAT) {
        encoder_update();
    }

    // Check if pending temperature should be pushed (2 second debounce)
    // Use fresh millis() to avoid underflow issues with pendingTempTime set in encoder callback
    if (hasPendingTemp && !isPushingTemp) {
        uint32_t currentTime = millis();
        if (currentTime >= pendingTempTime && (currentTime - pendingTempTime >= DEBOUNCE_DELAY_MS)) {
            checkAndPushTemperature();
        }
    }

    // Periodic refresh of current temperature (skip if user is adjusting)
    if (!hasPendingTemp && (now - lastRefreshTime >= REFRESH_INTERVAL_MS)) {
        fetchThermostatState();
        lastRefreshTime = now;
    }

    delay(1);
}

void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        display_update();

        if (millis() - startTime > WIFI_TIMEOUT_MS) {
            Serial.println("\nWiFi connection timeout!");
            ui_show_status("WiFi failed");
            return;
        }
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    ui_clear_status();
}

void fetchThermostatState() {
    Serial.println("Fetching thermostat state...");

    if (homey.getState(thermostatState)) {
        confirmedTemp = thermostatState.targetTemperature;

        // Only update pending temp if user isn't currently adjusting
        if (!hasPendingTemp) {
            pendingTemp = thermostatState.targetTemperature;
            ui_set_target_temp(thermostatState.targetTemperature);
        }

        ui_set_current_temp(thermostatState.currentTemperature);
        ui_set_heating(thermostatState.isHeating);

        // Clear status if we were waiting for confirmation
        if (waitingForConfirm) {
            waitingForConfirm = false;
            ui_clear_status();
            Serial.println("Temperature confirmed by Homey");
        }

        Serial.printf("Target: %.1f°C, Current: %.1f°C, Heating: %s\n",
                      thermostatState.targetTemperature,
                      thermostatState.currentTemperature,
                      thermostatState.isHeating ? "ON" : "OFF");
    } else {
        Serial.println("Failed to fetch thermostat state");
        Serial.println(homey.getLastError());
        ui_show_status("Connection failed");
    }
}

void checkAndPushTemperature() {
    // Don't push if temp hasn't actually changed
    if (pendingTemp == confirmedTemp) {
        hasPendingTemp = false;
        ui_clear_status();
        return;
    }

    Serial.printf("Pushing temperature: %.1f°C\n", pendingTemp);
    ui_show_status("Pushing temp");
    isPushingTemp = true;

    if (homey.setTargetTemperature(pendingTemp)) {
        confirmedTemp = pendingTemp;
        hasPendingTemp = false;
        waitingForConfirm = true;
        lastRefreshTime = millis();  // Reset refresh timer after push
        ui_show_status("Temp set");
        Serial.println("Temperature pushed successfully");
    } else {
        Serial.println("Failed to push temperature");
        Serial.println(homey.getLastError());
        ui_show_status("Push failed");

        // Keep hasPendingTemp true so we retry on next debounce cycle
        pendingTempTime = millis();  // Reset debounce timer for retry
    }

    isPushingTemp = false;
}

void onEncoderRotation(int8_t direction) {
    // Adjust pending temperature
    pendingTemp += direction * TEMP_STEP;

    // Clamp to valid range
    if (pendingTemp < TEMP_MIN) pendingTemp = TEMP_MIN;
    if (pendingTemp > TEMP_MAX) pendingTemp = TEMP_MAX;

    // Update UI immediately
    ui_set_target_temp(pendingTemp);

    // Mark as pending and record time for debounce
    hasPendingTemp = true;
    pendingTempTime = millis();
    ui_show_status("Setting temp");

    Serial.printf("Encoder: %s, Pending temp: %.1f°C\n",
                  direction > 0 ? "CW" : "CCW", pendingTemp);

    // Print debug info about encoder state transitions
    encoder_print_debug();
}

void onEncoderButton(bool pressed) {
    if (pressed && hasPendingTemp && !isPushingTemp) {
        // Immediate push on button press
        Serial.println("Button pressed - pushing immediately");
        pendingTempTime = 0;  // Trigger immediate push
    }
}
