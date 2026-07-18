#ifndef WEATHER_ICON_MAP_H
#define WEATHER_ICON_MAP_H

#include <lvgl.h>
#include "weather_icons.h"

// Shared mapping from a WMO weather interpretation code to a Lucide icon
// category and tint color, used by both the thermostat view (44px icons)
// and the clock view (24px icons) so the condition logic lives in one place.
// Codes: 0-1 clear, 2 partly cloudy, 3 overcast, 45/48 fog, 51-57 drizzle,
// 61-67 rain, 71-77 snow, 80-82 rain showers, 85/86 snow showers,
// 95-99 thunderstorm.
enum WeatherCat {
    WCAT_SUN = 0, WCAT_CLOUD_SUN, WCAT_CLOUDY, WCAT_FOG,
    WCAT_DRIZZLE, WCAT_RAIN, WCAT_SNOW, WCAT_LIGHTNING
};

static inline WeatherCat weather_cat_for_code(int wmo_code, lv_color_t* tint) {
    WeatherCat cat;
    lv_color_t t;

    if (wmo_code <= 1) {
        cat = WCAT_SUN;        t = lv_color_hex(0xFFC83C);  // Clear
    } else if (wmo_code == 2) {
        cat = WCAT_CLOUD_SUN;  t = lv_color_hex(0xFFC83C);  // Partly cloudy
    } else if (wmo_code == 45 || wmo_code == 48) {
        cat = WCAT_FOG;        t = lv_color_hex(0xC8C8C8);  // Fog
    } else if ((wmo_code >= 71 && wmo_code <= 77) || wmo_code == 85 || wmo_code == 86) {
        cat = WCAT_SNOW;       t = lv_color_hex(0xFFFFFF);  // Snow
    } else if (wmo_code >= 95) {
        cat = WCAT_LIGHTNING;  t = lv_color_hex(0xFFC83C);  // Thunderstorm
    } else if (wmo_code >= 80) {
        cat = WCAT_RAIN;       t = lv_color_hex(0x4FA8FF);  // Rain showers
    } else if (wmo_code >= 51) {
        cat = WCAT_DRIZZLE;    t = lv_color_hex(0x4FA8FF);  // Drizzle / rain
    } else {
        cat = WCAT_CLOUDY;     t = lv_color_hex(0xC8C8C8);  // Overcast
    }

    if (tint) *tint = t;
    return cat;
}

// 44px icons for the thermostat view
static inline const lv_img_dsc_t* weather_icon_for_code(int wmo_code, lv_color_t* tint) {
    static const lv_img_dsc_t* const big[] = {
        &wi_sun, &wi_cloud_sun, &wi_cloudy, &wi_cloud_fog,
        &wi_cloud_drizzle, &wi_cloud_rain, &wi_cloud_snow, &wi_cloud_lightning,
    };
    return big[weather_cat_for_code(wmo_code, tint)];
}

// 24px icons for the clock view
static inline const lv_img_dsc_t* weather_icon_small_for_code(int wmo_code, lv_color_t* tint) {
    static const lv_img_dsc_t* const small[] = {
        &wi_sun_s, &wi_cloud_sun_s, &wi_cloudy_s, &wi_cloud_fog_s,
        &wi_cloud_drizzle_s, &wi_cloud_rain_s, &wi_cloud_snow_s, &wi_cloud_lightning_s,
    };
    return small[weather_cat_for_code(wmo_code, tint)];
}

#endif // WEATHER_ICON_MAP_H
