#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/encoder.h"
#include "homey/homey_api.h"
#include "ui/ui.h"

// Global instances
HomeyAPI homey;
ThermostatState thermostatState;

// Temperature control state
static float pendingTemp = TEMP_DEFAULT;
static float lastSentTemp = TEMP_DEFAULT;
static bool tempChanged = false;
static uint32_t lastChangeTime = 0;
static uint32_t lastRefreshTime = 0;

// Forward declarations
void onEncoderRotation(int8_t direction);
void onEncoderButton(bool pressed);
void connectWiFi();
void fetchThermostatState();
void sendTemperatureUpdate();

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Thermostat Control ===");
    Serial.println("Initializing...");

    // Initialize display and LVGL
    if (!display_init()) {
        Serial.println("Display initialization failed!");
        while (1) delay(100);
    }

    // Initialize UI
    ui_init();
    ui_set_state(UI_STATE_CONNECTING);

    // Initialize encoder
    if (!encoder_init()) {
        Serial.println("Encoder initialization failed!");
    }
    encoder_set_rotation_callback(onEncoderRotation);
    encoder_set_button_callback(onEncoderButton);
    encoder_register_lvgl();

    // Update display
    display_update();

    // Connect to WiFi
    connectWiFi();

    // Initialize Homey API
    homey.begin(HOMEY_IP, HOMEY_API_KEY, THERMOSTAT_DEVICE_ID);

    // Fetch initial thermostat state
    fetchThermostatState();

    Serial.println("Initialization complete!");
}

void loop() {
    static uint32_t lastLvglUpdate = 0;
    uint32_t now = millis();

    // Update LVGL (target ~30ms = ~33fps)
    if (now - lastLvglUpdate >= 5) {
        lastLvglUpdate = now;
        display_update();
    }

    // Process encoder callbacks
    encoder_update();

    // Send temperature update after debounce delay
    if (tempChanged && (now - lastChangeTime >= DEBOUNCE_DELAY_MS)) {
        sendTemperatureUpdate();
    }

    // Periodic refresh of current temperature
    if (now - lastRefreshTime >= REFRESH_INTERVAL_MS) {
        fetchThermostatState();
        lastRefreshTime = now;
    }

    // Small delay to prevent watchdog issues
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
            ui_set_state(UI_STATE_ERROR);
            ui_show_error("WiFi failed");
            return;
        }
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void fetchThermostatState() {
    Serial.println("Fetching thermostat state...");

    if (homey.getState(thermostatState)) {
        pendingTemp = thermostatState.targetTemperature;
        lastSentTemp = thermostatState.targetTemperature;

        ui_set_target_temp(thermostatState.targetTemperature);
        ui_set_current_temp(thermostatState.currentTemperature);
        ui_set_state(UI_STATE_NORMAL);

        Serial.printf("Target: %.1f°C, Current: %.1f°C\n",
                      thermostatState.targetTemperature,
                      thermostatState.currentTemperature);
    } else {
        Serial.println("Failed to fetch thermostat state");
        Serial.println(homey.getLastError());
        ui_set_state(UI_STATE_ERROR);
        ui_show_error("Connection failed");
    }
}

void sendTemperatureUpdate() {
    if (!tempChanged || pendingTemp == lastSentTemp) {
        tempChanged = false;
        return;
    }

    Serial.printf("Sending temperature: %.1f°C\n", pendingTemp);
    ui_set_state(UI_STATE_SENDING);
    display_update();

    if (homey.setTargetTemperature(pendingTemp)) {
        lastSentTemp = pendingTemp;
        ui_animate_confirm();

        // Reset to normal after animation
        delay(500);
        display_update();
        ui_set_state(UI_STATE_NORMAL);
    } else {
        Serial.println("Failed to set temperature");
        Serial.println(homey.getLastError());
        ui_set_state(UI_STATE_ERROR);
        ui_show_error("Send failed");

        // Revert to last known good temperature
        pendingTemp = lastSentTemp;
        ui_set_target_temp(pendingTemp);
    }

    tempChanged = false;
}

void onEncoderRotation(int8_t direction) {
    // Adjust pending temperature
    pendingTemp += direction * TEMP_STEP;

    // Clamp to valid range
    if (pendingTemp < TEMP_MIN) pendingTemp = TEMP_MIN;
    if (pendingTemp > TEMP_MAX) pendingTemp = TEMP_MAX;

    // Update UI immediately
    ui_set_target_temp(pendingTemp);
    ui_set_state(UI_STATE_ADJUSTING);

    // Mark as changed and record time for debounce
    tempChanged = true;
    lastChangeTime = millis();

    Serial.printf("Encoder: %s, Pending temp: %.1f°C\n",
                  direction > 0 ? "CW" : "CCW", pendingTemp);
}

void onEncoderButton(bool pressed) {
    if (pressed && tempChanged) {
        // Immediate send on button press
        Serial.println("Button pressed - sending immediately");
        lastChangeTime = 0;  // Trigger immediate send
    }
}
