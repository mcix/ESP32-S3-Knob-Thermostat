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
#include "weather/weather_api.h"

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

// Async HTTP worker (core 0, loop runs on core 1) so blocking requests
// never freeze the display. The loop owns activeJob and only hands it to
// the worker via a task notification; the worker signals back with jobDone.
enum HttpJob : uint8_t { JOB_NONE, JOB_FETCH, JOB_PUSH, JOB_WEATHER };
static TaskHandle_t httpTaskHandle = nullptr;
static volatile HttpJob activeJob = JOB_NONE;
static volatile bool jobDone = false;
static volatile bool jobSuccess = false;
static float jobPushTemp = 0;                // Input for JOB_PUSH
static ThermostatState jobState;             // Output of JOB_FETCH
static WeatherState jobWeather;              // Output of JOB_WEATHER
static uint32_t lastWeatherTime = 0;         // 0 = fetch as soon as possible

// Forward declarations
void onEncoderRotation(int8_t direction);
void onEncoderButton(bool pressed);
void connectWiFi();
void checkWiFiConnection();
bool requestStateFetch();
void processHttpResult();
void checkAndPushTemperature();
void httpTask(void* param);

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

    // Start the HTTP worker; after this only the worker touches `homey`.
    // Stack sized for TLS (weather fetch uses HTTPS).
    xTaskCreatePinnedToCore(httpTask, "http_worker", 12288, nullptr, 1, &httpTaskHandle, 0);

    // Fetch initial thermostat state
    requestStateFetch();

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

    // Process encoder callbacks (only affects thermostat view).
    // From other views, a deliberate turn wakes the thermostat view: it
    // takes ENCODER_WAKE_STEPS within a short window, so a single phantom
    // pulse can't bounce the view over. The waking steps are discarded,
    // so they don't change the temperature.
    if (views_current() == VIEW_THERMOSTAT) {
        encoder_update();
    } else {
        static uint8_t wakeSteps = 0;
        static uint32_t lastWakeStep = 0;
        int steps = encoder_flush();
        if (steps > 0) {
            if (now - lastWakeStep > ENCODER_WAKE_WINDOW_MS) {
                wakeSteps = 0;  // Previous activity was too long ago; restart
            }
            wakeSteps += steps;
            lastWakeStep = now;
            if (wakeSteps >= ENCODER_WAKE_STEPS) {
                wakeSteps = 0;
                Serial.println("Knob turned - switching to thermostat view");
                views_switch(VIEW_THERMOSTAT);
            }
        }
    }

    // Monitor WiFi link and reconnect if it drops
    checkWiFiConnection();

    // Handle results from completed HTTP requests
    processHttpResult();

    // Check if pending temperature should be pushed (2 second debounce)
    // Use fresh millis() to avoid underflow issues with pendingTempTime set in encoder callback
    // Skip while WiFi is down; the push fires as soon as the link is back
    if (hasPendingTemp && !isPushingTemp && WiFi.status() == WL_CONNECTED) {
        uint32_t currentTime = millis();
        if (currentTime >= pendingTempTime && (currentTime - pendingTempTime >= DEBOUNCE_DELAY_MS)) {
            checkAndPushTemperature();
        }
    }

    // Periodic refresh of current temperature (skip if user is adjusting or WiFi is down)
    if (!hasPendingTemp && !isPushingTemp && WiFi.status() == WL_CONNECTED &&
        (now - lastRefreshTime >= REFRESH_INTERVAL_MS)) {
        if (requestStateFetch()) {
            lastRefreshTime = now;
        }
    }

    // Periodic outside weather refresh (lower priority than thermostat jobs)
    if (WiFi.status() == WL_CONNECTED && activeJob == JOB_NONE &&
        (lastWeatherTime == 0 || now - lastWeatherTime >= WEATHER_REFRESH_INTERVAL_MS)) {
        Serial.println("Fetching outside weather...");
        jobDone = false;
        activeJob = JOB_WEATHER;
        xTaskNotifyGive(httpTaskHandle);
        lastWeatherTime = now;
    }

    delay(1);
}

void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);       // Don't wear out NVS flash with credentials on every begin()
    WiFi.setAutoReconnect(true);  // Let the WiFi driver retry on disconnect events
    WiFi.setSleep(false);         // Modem sleep causes silent drops and latency spikes
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

void checkWiFiConnection() {
    static uint32_t lastCheck = 0;
    static uint32_t disconnectedSince = 0;
    static uint32_t lastReconnectAttempt = 0;
    static bool wasConnected = true;

    uint32_t now = millis();
    if (now - lastCheck < WIFI_CHECK_INTERVAL_MS) {
        return;
    }
    lastCheck = now;

    if (WiFi.status() == WL_CONNECTED) {
        if (!wasConnected) {
            Serial.print("WiFi reconnected! IP address: ");
            Serial.println(WiFi.localIP());
            ui_clear_status();
            lastRefreshTime = 0;  // Trigger immediate state refresh
            wasConnected = true;
        }
        return;
    }

    if (wasConnected) {
        wasConnected = false;
        disconnectedSince = now;
        lastReconnectAttempt = now;  // Give auto-reconnect a chance first
        Serial.println("WiFi connection lost!");
        ui_show_status("WiFi lost");
        return;
    }

    // Last resort: the WiFi stack can wedge in a state it never recovers from
    if (now - disconnectedSince >= WIFI_REBOOT_AFTER_MS) {
        Serial.println("WiFi down too long - rebooting");
        delay(100);  // Let serial output flush
        ESP.restart();
    }

    if (now - lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
        lastReconnectAttempt = now;
        Serial.println("Attempting WiFi reconnect...");
        ui_show_status("WiFi reconnecting");
        WiFi.disconnect();
        WiFi.reconnect();
    }
}

bool requestStateFetch() {
    if (activeJob != JOB_NONE) {
        return false;  // Worker busy; caller retries later
    }

    Serial.println("Fetching thermostat state...");
    jobDone = false;
    activeJob = JOB_FETCH;
    xTaskNotifyGive(httpTaskHandle);
    return true;
}

void checkAndPushTemperature() {
    // Don't push if temp hasn't actually changed
    if (pendingTemp == confirmedTemp) {
        hasPendingTemp = false;
        ui_clear_status();
        return;
    }

    if (activeJob != JOB_NONE) {
        return;  // Worker busy; retried on next loop pass
    }

    Serial.printf("Pushing temperature: %.1f°C\n", pendingTemp);
    ui_show_status("Pushing temp");
    isPushingTemp = true;
    jobPushTemp = pendingTemp;
    jobDone = false;
    activeJob = JOB_PUSH;
    xTaskNotifyGive(httpTaskHandle);
}

void processHttpResult() {
    if (!jobDone) {
        return;
    }

    HttpJob job = activeJob;
    bool ok = jobSuccess;
    jobDone = false;
    activeJob = JOB_NONE;

    if (job == JOB_FETCH) {
        if (ok) {
            thermostatState = jobState;
            confirmedTemp = thermostatState.targetTemperature;

            // Only update pending temp if user isn't currently adjusting
            if (!hasPendingTemp) {
                pendingTemp = thermostatState.targetTemperature;
                ui_set_target_temp(thermostatState.targetTemperature);
            }

            ui_set_current_temp(thermostatState.currentTemperature);
            ui_set_heating(thermostatState.isHeating);
            views_set_indoor_temp(thermostatState.currentTemperature);

            // Clear status if we were waiting for confirmation
            if (waitingForConfirm) {
                waitingForConfirm = false;
                ui_clear_status();
                Serial.println("Temperature confirmed by Homey");
            }
        } else {
            Serial.println("Failed to fetch thermostat state");
            ui_show_status("Connection failed");
        }
    } else if (job == JOB_PUSH) {
        isPushingTemp = false;

        if (ok) {
            confirmedTemp = jobPushTemp;
            waitingForConfirm = true;
            lastRefreshTime = millis();  // Reset refresh timer after push
            ui_show_status("Temp set");
            Serial.println("Temperature pushed successfully");

            // The knob may have moved during the push; if so, leave the new
            // value pending so it gets pushed after its own debounce
            if (pendingTemp == confirmedTemp) {
                hasPendingTemp = false;
            }
        } else {
            Serial.println("Failed to push temperature");
            ui_show_status("Push failed");

            // Keep hasPendingTemp true so we retry on next debounce cycle
            pendingTempTime = millis();  // Reset debounce timer for retry
        }
    } else if (job == JOB_WEATHER) {
        if (ok) {
            ui_set_weather(jobWeather.temperature, jobWeather.weatherCode);
            views_set_outdoor(jobWeather.temperature, jobWeather.weatherCode);
        } else {
            // Retry in a minute instead of waiting the full refresh interval
            lastWeatherTime = millis() - (WEATHER_REFRESH_INTERVAL_MS - 60000);
        }
    }
}

void httpTask(void* param) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (activeJob == JOB_FETCH) {
            jobSuccess = homey.getState(jobState);
            if (!jobSuccess) Serial.println(homey.getLastError());
        } else if (activeJob == JOB_PUSH) {
            jobSuccess = homey.setTargetTemperature(jobPushTemp);
            if (!jobSuccess) Serial.println(homey.getLastError());
        } else if (activeJob == JOB_WEATHER) {
            jobSuccess = weather_fetch(jobWeather);
        }
        jobDone = true;
    }
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
    views_reset_activity();
    ui_show_status("Setting temp");

    Serial.printf("Encoder: %s, Pending temp: %.1f°C\n",
                  direction > 0 ? "CW" : "CCW", pendingTemp);
}

void onEncoderButton(bool pressed) {
    if (pressed && hasPendingTemp && !isPushingTemp) {
        // Immediate push on button press
        Serial.println("Button pressed - pushing immediately");
        pendingTempTime = 0;  // Trigger immediate push
    }
}
