#ifndef WW_WEATHER_H_
#define WW_WEATHER_H_

#include "units.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define MAX_HOURS 48
#define MAX_DAYS 7

typedef struct {
  char time[20]; /* local ISO-8601, "2026-10-17T14:00" */
  double temperature;
  double precipitation;
  int precip_probability;
  int code;
  bool is_day;
} weather_hour;

typedef struct {
  char date[12]; /* "2026-10-17" */
  int code;
  double temp_max;
  double temp_min;
  double precipitation;
  int precip_probability;
  double uv_max;
  double wind_max;
  char sunrise[20];
  char sunset[20];
} weather_day;

typedef struct {
  time_t fetched_at;
  bool stale; /* served from an expired cache because the fetch failed */
  char timezone[64];
  char time[20]; /* observation time, local ISO-8601 */
  double elevation;

  double temperature;
  double apparent_temperature;
  double humidity;
  double dew_point;
  double precipitation;
  double cloud_cover;
  double pressure_hpa;
  double wind_speed;
  double wind_gusts;
  double wind_direction;
  double uv_index;
  double visibility_m;
  int code;
  bool is_day;

  weather_hour hours[MAX_HOURS]; /* starting with the next full hour */
  int n_hours;
  weather_day days[MAX_DAYS]; /* starting today */
  int n_days;

  units_t units;
} weather_t;

/* Fetches weather, using the on-disk cache when younger than `ttl`.
 * With `force` the cache is bypassed (but still used as a fallback). */
bool weather_get(double lat, double lon, const units_t *units, int ttl,
                 bool force, weather_t *out, char *err, size_t errlen);

/* WMO weather interpretation codes */
const char *wmo_description(int code);
const char *wmo_icon(int code, bool is_day);
/* CSS-friendly condition class: clear, partly-cloudy, cloudy, fog, ... */
const char *wmo_class(int code);

/* Compass helpers */
const char *wind_compass(double degrees);
const char *wind_arrow(double degrees);

#endif // !WW_WEATHER_H_
