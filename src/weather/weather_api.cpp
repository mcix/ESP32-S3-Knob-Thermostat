#include "weather_api.h"
#include "../config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool weather_fetch(WeatherState& out) {
    WiFiClientSecure client;
    client.setInsecure();  // Public read-only data; skip certificate validation

    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + WEATHER_LAT +
                 "&longitude=" + WEATHER_LON +
                 "&current=temperature_2m,weather_code&models=knmi_seamless";

    Serial.print("GET ");
    Serial.println(url);

    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("Weather fetch: http.begin failed");
        return false;
    }
    // TLS handshake on a cold connection can be slow; allow more time than
    // the plain-HTTP Homey requests. Runs on the worker task, so a long
    // wait here never blocks the UI.
    http.setConnectTimeout(15000);
    http.setTimeout(15000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("Weather fetch failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getString());
    http.end();

    if (error) {
        Serial.printf("Weather JSON parse error: %s\n", error.c_str());
        return false;
    }

    JsonObject current = doc["current"];
    if (!current["temperature_2m"].is<float>()) {
        Serial.println("Weather response missing temperature");
        return false;
    }

    out.temperature = current["temperature_2m"].as<float>();
    out.weatherCode = current["weather_code"].as<int>();

    Serial.printf("Weather: %.1f°C, WMO code %d\n", out.temperature, out.weatherCode);
    return true;
}
