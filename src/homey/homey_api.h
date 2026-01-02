#ifndef HOMEY_API_H
#define HOMEY_API_H

#include <Arduino.h>

/**
 * Thermostat state structure
 */
struct ThermostatState {
    float targetTemperature;
    float currentTemperature;
    bool isHeating;          // True when central heating is active
    bool connected;
    String deviceName;
    String lastError;
};

/**
 * Homey API Client for thermostat control
 */
class HomeyAPI {
public:
    HomeyAPI();

    /**
     * Initialize the API client
     * @param ip Homey Pro local IP address
     * @param apiKey Homey API key (Bearer token)
     * @param deviceId Thermostat device ID
     * @return true if configuration is valid
     */
    bool begin(const char* ip, const char* apiKey, const char* deviceId);

    /**
     * Fetch current thermostat state from Homey
     * @param state Output parameter for thermostat state
     * @return true if successful
     */
    bool getState(ThermostatState& state);

    /**
     * Set target temperature
     * @param temperature Target temperature in Celsius
     * @return true if successful
     */
    bool setTargetTemperature(float temperature);

    /**
     * Get last error message
     * @return Error string
     */
    String getLastError() const;

    /**
     * Check if connected to Homey
     * @return true if last request was successful
     */
    bool isConnected() const;

private:
    String _baseUrl;
    String _apiKey;
    String _deviceId;
    String _lastError;
    bool _connected;

    /**
     * Make HTTP GET request
     * @param endpoint API endpoint
     * @param response Output for response body
     * @return HTTP status code
     */
    int httpGet(const String& endpoint, String& response);

    /**
     * Make HTTP PUT request
     * @param endpoint API endpoint
     * @param body Request body (JSON)
     * @param response Output for response body
     * @return HTTP status code
     */
    int httpPut(const String& endpoint, const String& body, String& response);
};

#endif // HOMEY_API_H
