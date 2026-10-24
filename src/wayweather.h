#ifndef WAYWEATHER_H_
#define WAYWEATHER_H_

/*
 * libwayweather - the weather, place search and geolocation of wayweather
 * as a C library.
 *
 * Link with -lwayweather (or `pkg-config --cflags --libs wayweather`).
 *
 * Usage:
 *   ww_init() once at startup (before starting threads), ww_cleanup() at
 *   exit. Afterwards all functions may be called from any thread.
 *
 * Two kinds of functions are provided:
 *   - ww_*_get / ww_geocode / ww_geolocate fill plain structs with raw
 *     values, for programs that want to format the data themselves.
 *   - ww_waybar_json / ww_format return heap allocated strings, exactly
 *     what the wayweather binary prints. Release them with free().
 *
 * Functions returning int give 0 on success and -1 on failure, unless noted
 * otherwise. On failure a human readable reason is written to `err`
 * (when non-NULL). Numbers are always parsed and printed with "." as the
 * decimal separator, whatever the program's locale.
 *
 * Forecasts and positions are cached in $XDG_CACHE_HOME/wayweather, shared
 * with the wayweather binary. Functions that mention "the user's config"
 * read $XDG_CONFIG_HOME/wayweather/config.json (see ww_set_config_path).
 *
 * Weather data by Open-Meteo.com (CC BY 4.0).
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define WW_API __attribute__((visibility("default")))
#else
#define WW_API
#endif

/* ------------------------------------------------------------------------ */
/* General                                                                  */
/* ------------------------------------------------------------------------ */

/* Version of the library, e.g. "3.0.1". */
WW_API const char *ww_version(void);

/* Initialises libcurl and the cache paths. Not thread-safe: call once
 * before any other function. Returns 0 on success. */
WW_API int ww_init(void);
WW_API void ww_cleanup(void);

/* 0 (default) = silent, 1 = errors and warnings, 2 = info, 3 = debug.
 * Messages go to stderr. */
WW_API void ww_set_verbosity(int level);

/* Use another config file than $XDG_CONFIG_HOME/wayweather/config.json.
 * NULL restores the default. */
WW_API void ww_set_config_path(const char *path);

/* ------------------------------------------------------------------------ */
/* Units                                                                    */
/* ------------------------------------------------------------------------ */

typedef enum { WW_TEMP_CELSIUS, WW_TEMP_FAHRENHEIT } ww_temp_unit;
typedef enum { WW_WIND_KMH, WW_WIND_MS, WW_WIND_MPH, WW_WIND_KN } ww_wind_unit;
typedef enum { WW_PRECIP_MM, WW_PRECIP_INCH } ww_precip_unit;
typedef enum {
  WW_PRESSURE_HPA,
  WW_PRESSURE_INHG,
  WW_PRESSURE_MMHG,
  WW_PRESSURE_KPA
} ww_pressure_unit;
typedef enum { WW_CLOCK_24H, WW_CLOCK_12H } ww_clock;

typedef struct {
  ww_temp_unit temperature;
  ww_wind_unit wind;
  ww_precip_unit precipitation;
  ww_pressure_unit pressure;
  ww_clock clock;
} ww_units;

/* °C, km/h, mm, hPa, 24h */
WW_API void ww_units_metric(ww_units *units);
/* °F, mph, inch, inHg, 12h */
WW_API void ww_units_imperial(ww_units *units);

/* Unit symbols, e.g. "°C", "km/h", "in", "hPa"; "" for invalid values */
WW_API const char *ww_temp_label(ww_temp_unit unit);
WW_API const char *ww_wind_label(ww_wind_unit unit);
WW_API const char *ww_precip_label(ww_precip_unit unit);
WW_API const char *ww_pressure_label(ww_pressure_unit unit);

/* Converts a pressure in hPa (ww_weather.pressure_hpa) */
WW_API double ww_pressure_from_hpa(double hpa, ww_pressure_unit unit);

/* ------------------------------------------------------------------------ */
/* Locations                                                                */
/* ------------------------------------------------------------------------ */

#define WW_FIELD_LEN 96

typedef struct {
  char name[WW_FIELD_LEN]; /* optional label, e.g. "Home" */
  char city[WW_FIELD_LEN];
  char region[WW_FIELD_LEN];
  char country[WW_FIELD_LEN];
  double latitude;
  double longitude;
  ww_units units;
  int is_default; /* saved locations: the user's default location */
} ww_location;

/* Searches places by name (Open-Meteo geocoding). "City, Region/Country"
 * narrows the results by the part after the comma. US results default to
 * imperial units, everything else to metric.
 * Returns the number of results written to `out` (0..max) or -1. */
WW_API int ww_geocode(const char *query, ww_location *out, int max,
                      char *err, size_t errlen);

/* Fills city/region/country for coordinates (OpenStreetMap Nominatim).
 * Results are cached on disk. */
WW_API int ww_reverse_geocode(double latitude, double longitude,
                              ww_location *loc);

typedef enum {
  WW_GEO_AUTO, /* gpsd, then GeoClue (if built in), then IP */
  WW_GEO_GPSD,
  WW_GEO_GEOCLUE,
  WW_GEO_IP
} ww_geo_provider;

/* Determines the device position. Fills latitude, longitude, city, region
 * and country of `out` (other fields untouched). A fix younger than
 * `cache_ttl` seconds is reused (0 = always locate). `source` receives the
 * provider that answered, e.g. "gpsd", "geoclue", "ip (cached)".
 * gpsd is expected at localhost:2947; `timeout` is per provider. */
WW_API int ww_geolocate(ww_geo_provider provider, int timeout, int cache_ttl,
                        ww_location *out, char *source, size_t source_len,
                        char *err, size_t errlen);

/* What the user's wayweather shows right now: the geolocated position if
 * geolocation is enabled, otherwise the current location. */
WW_API int ww_current_location(ww_location *out, char *err, size_t errlen);

/* The user's saved locations. Returns the count and stores a malloc()ed
 * array in *out (release with free(); NULL when the count is 0), or -1. */
WW_API int ww_saved_locations(ww_location **out, char *err, size_t errlen);

/* ------------------------------------------------------------------------ */
/* Weather                                                                  */
/* ------------------------------------------------------------------------ */

#define WW_MAX_HOURS 48
#define WW_MAX_DAYS 7

typedef struct {
  char time[20]; /* local time, ISO 8601: "2026-10-17T14:00" */
  double temperature;
  double precipitation;
  int precip_probability; /* % */
  int code;               /* WMO weather code */
  int is_day;
} ww_hour;

typedef struct {
  char date[12]; /* "2026-10-17" */
  int code;
  double temp_max;
  double temp_min;
  double precipitation;
  int precip_probability;
  double uv_max;
  double wind_max;
  char sunrise[20]; /* local time, ISO 8601 */
  char sunset[20];
} ww_day;

/* Values are in the units requested; missing values are NaN. */
typedef struct {
  long long fetched_at; /* unix time of the download */
  int stale;            /* 1: the fetch failed, this is older cached data */
  char timezone[64];    /* e.g. "Europe/Vienna" */
  char time[20];        /* observation time, local ISO 8601 */
  double elevation;     /* metres */

  double temperature;
  double apparent_temperature;
  double humidity;   /* % */
  double dew_point;
  double precipitation;
  double cloud_cover; /* % */
  double pressure_hpa; /* convert with ww_pressure_from_hpa() */
  double wind_speed;
  double wind_gusts;
  double wind_direction; /* degrees, where the wind comes from */
  double uv_index;
  double visibility_m;
  int code; /* WMO weather code */
  int is_day;

  ww_hour hours[WW_MAX_HOURS]; /* starting with the next full hour */
  int n_hours;
  ww_day days[WW_MAX_DAYS]; /* starting today */
  int n_days;

  ww_units units;
} ww_weather;

/* Fetches current weather and forecast for coordinates. A cached forecast
 * younger than `cache_ttl` seconds is reused (0 = always download); if the
 * download fails, older cached data is returned with `stale` set.
 * `units` may be NULL for metric. */
WW_API int ww_weather_get(double latitude, double longitude,
                          const ww_units *units, int cache_ttl,
                          ww_weather *out, char *err, size_t errlen);

/* Same for a location, using its coordinates and units */
WW_API int ww_weather_for(const ww_location *loc, int cache_ttl,
                          ww_weather *out, char *err, size_t errlen);

/* WMO code helpers */
WW_API const char *ww_wmo_description(int code); /* "Light rain" */
WW_API const char *ww_wmo_icon(int code, int is_day); /* Nerd Font glyph */
/* CSS-friendly class: clear, partly-cloudy, cloudy, fog, drizzle, rain,
 * freezing, snow, thunder */
WW_API const char *ww_wmo_class(int code);

/* "N", "NE", ... and an arrow showing where the wind blows to */
WW_API const char *ww_wind_compass(double degrees);
WW_API const char *ww_wind_arrow(double degrees);

/* ------------------------------------------------------------------------ */
/* Formatting                                                               */
/* ------------------------------------------------------------------------ */

/* Expands wayweather placeholders such as "{icon} {temp}{tunit}" (see
 * `wayweather help format`). Temperatures use `decimals` decimals (0-2),
 * `icons` toggles Nerd Font glyphs. Returns a malloc()ed string. */
WW_API char *ww_format(const char *tmpl, const ww_location *loc,
                       const ww_weather *weather, int decimals, int icons);

/* One line of Waybar JSON exactly like `wayweather get`, using the user's
 * config. `location` selects a saved location (1-based number, name, city
 * or "default"); NULL shows the current location. On failure an error line
 * (class "error") is returned. Returns a malloc()ed string, NULL only when
 * out of memory. */
WW_API char *ww_waybar_json(const char *location);

#ifdef __cplusplus
}
#endif

#endif // !WAYWEATHER_H_
