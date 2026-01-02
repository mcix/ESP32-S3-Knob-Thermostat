#include "homey_api.h"
#include "../config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

HomeyAPI::HomeyAPI()
    : _connected(false) {
}

bool HomeyAPI::begin(const char* ip, const char* apiKey, const char* deviceId) {
    if (!ip || !apiKey || !deviceId) {
        _lastError = "Invalid configuration";
        return false;
    }

    _baseUrl = String("http://") + ip + "/api/manager/devices/device/";
    _apiKey = apiKey;
    _deviceId = deviceId;

    Serial.println("HomeyAPI initialized");
    Serial.print("Base URL: ");
    Serial.println(_baseUrl);

    return true;
}

int HomeyAPI::httpGet(const String& endpoint, String& response) {
    HTTPClient http;

    String url = _baseUrl + endpoint;
    Serial.print("GET ");
    Serial.println(url);

    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + _apiKey);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(API_TIMEOUT_MS);

    int httpCode = http.GET();

    if (httpCode > 0) {
        response = http.getString();
    } else {
        _lastError = http.errorToString(httpCode);
    }

    http.end();
    return httpCode;
}

int HomeyAPI::httpPut(const String& endpoint, const String& body, String& response) {
    HTTPClient http;

    String url = _baseUrl + endpoint;
    Serial.print("PUT ");
    Serial.println(url);
    Serial.print("Body: ");
    Serial.println(body);

    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + _apiKey);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(API_TIMEOUT_MS);

    int httpCode = http.PUT(body);

    if (httpCode > 0) {
        response = http.getString();
    } else {
        _lastError = http.errorToString(httpCode);
    }

    http.end();
    return httpCode;
}

bool HomeyAPI::getState(ThermostatState& state) {
    state.connected = false;
    state.targetTemperature = TEMP_DEFAULT;
    state.currentTemperature = TEMP_DEFAULT;

    String response;
    int httpCode = httpGet(_deviceId, response);

    if (httpCode != 200) {
        _lastError = "HTTP error: " + String(httpCode);
        _connected = false;
        return false;
    }

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        _lastError = "JSON parse error: " + String(error.c_str());
        _connected = false;
        return false;
    }

    // Extract device name
    if (doc["name"].is<const char*>()) {
        state.deviceName = doc["name"].as<String>();
    }

    // Extract capabilities
    JsonObject capabilities = doc["capabilitiesObj"];

    // Get target temperature
    if (capabilities["target_temperature"].is<JsonObject>()) {
        JsonObject targetTemp = capabilities["target_temperature"];
        if (targetTemp["value"].is<float>()) {
            state.targetTemperature = targetTemp["value"].as<float>();
        }
    }

    // Get current/measured temperature
    if (capabilities["measure_temperature"].is<JsonObject>()) {
        JsonObject measureTemp = capabilities["measure_temperature"];
        if (measureTemp["value"].is<float>()) {
            state.currentTemperature = measureTemp["value"].as<float>();
        }
    }

    state.connected = true;
    _connected = true;
    _lastError = "";

    Serial.printf("Thermostat: %s, Target: %.1f°C, Current: %.1f°C\n",
                  state.deviceName.c_str(),
                  state.targetTemperature,
                  state.currentTemperature);

    return true;
}

bool HomeyAPI::setTargetTemperature(float temperature) {
    // Clamp temperature to valid range
    if (temperature < TEMP_MIN) temperature = TEMP_MIN;
    if (temperature > TEMP_MAX) temperature = TEMP_MAX;

    // Build JSON body
    JsonDocument doc;
    doc["value"] = temperature;

    String body;
    serializeJson(doc, body);

    // Make PUT request to capability endpoint
    String endpoint = _deviceId + "/capability/target_temperature";
    String response;
    int httpCode = httpPut(endpoint, body, response);

    if (httpCode != 200) {
        _lastError = "HTTP error: " + String(httpCode) + " - " + response;
        _connected = false;
        Serial.println(_lastError);
        return false;
    }

    _connected = true;
    _lastError = "";

    Serial.printf("Temperature set to %.1f°C\n", temperature);
    return true;
}

String HomeyAPI::getLastError() const {
    return _lastError;
}

bool HomeyAPI::isConnected() const {
    return _connected;
}
