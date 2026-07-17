#ifndef WEATHER_API_H
#define WEATHER_API_H

/**
 * Outside weather state
 */
struct WeatherState {
    float temperature;   // Outside temperature in Celsius
    int weatherCode;     // WMO weather interpretation code
};

/**
 * Fetch the current outside temperature and weather condition for the
 * location configured in config.h (KNMI HARMONIE-AROME model via the
 * Open-Meteo API). Blocking; call from the HTTP worker task.
 * @param out Output parameter for the weather state
 * @return true if successful
 */
bool weather_fetch(WeatherState& out);

#endif // WEATHER_API_H
