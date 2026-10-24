#include "weather.h"
#include "http.h"
#include "log.h"
#include "util.h"

#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_URL "https://api.open-meteo.com/v1/forecast"
#define HTTP_TIMEOUT 15

/* ---------- WMO codes ---------- */

/* Material Design Icons from Nerd Fonts */
#define I_SUNNY "\U000F0599"
#define I_NIGHT "\U000F0594"
#define I_PARTLY "\U000F0595"
#define I_NIGHT_PARTLY "\U000F0F31"
#define I_CLOUDY "\U000F0590"
#define I_FOG "\U000F0591"
#define I_HAIL "\U000F0592"
#define I_LIGHTNING "\U000F0593"
#define I_POURING "\U000F0596"
#define I_RAINY "\U000F0597"
#define I_SNOWY "\U000F0598"
#define I_SNOWY_RAINY "\U000F067F"
#define I_LIGHTNING_RAINY "\U000F067E"
#define I_SNOWY_HEAVY "\U000F0F36"

typedef struct {
  int code;
  const char *description;
  const char *icon_day;
  const char *icon_night;
  const char *css;
} wmo_entry;

static const wmo_entry WMO[] = {
    {0, "Clear sky", I_SUNNY, I_NIGHT, "clear"},
    {1, "Mainly clear", I_SUNNY, I_NIGHT, "clear"},
    {2, "Partly cloudy", I_PARTLY, I_NIGHT_PARTLY, "partly-cloudy"},
    {3, "Overcast", I_CLOUDY, I_CLOUDY, "cloudy"},
    {45, "Fog", I_FOG, I_FOG, "fog"},
    {48, "Depositing rime fog", I_FOG, I_FOG, "fog"},
    {51, "Light drizzle", I_RAINY, I_RAINY, "drizzle"},
    {53, "Drizzle", I_RAINY, I_RAINY, "drizzle"},
    {55, "Dense drizzle", I_RAINY, I_RAINY, "drizzle"},
    {56, "Light freezing drizzle", I_SNOWY_RAINY, I_SNOWY_RAINY, "freezing"},
    {57, "Freezing drizzle", I_SNOWY_RAINY, I_SNOWY_RAINY, "freezing"},
    {61, "Light rain", I_RAINY, I_RAINY, "rain"},
    {63, "Rain", I_POURING, I_POURING, "rain"},
    {65, "Heavy rain", I_POURING, I_POURING, "rain"},
    {66, "Light freezing rain", I_SNOWY_RAINY, I_SNOWY_RAINY, "freezing"},
    {67, "Freezing rain", I_SNOWY_RAINY, I_SNOWY_RAINY, "freezing"},
    {71, "Light snow", I_SNOWY, I_SNOWY, "snow"},
    {73, "Snow", I_SNOWY, I_SNOWY, "snow"},
    {75, "Heavy snow", I_SNOWY_HEAVY, I_SNOWY_HEAVY, "snow"},
    {77, "Snow grains", I_HAIL, I_HAIL, "snow"},
    {80, "Light showers", I_RAINY, I_RAINY, "rain"},
    {81, "Showers", I_POURING, I_POURING, "rain"},
    {82, "Violent showers", I_POURING, I_POURING, "rain"},
    {85, "Snow showers", I_SNOWY_HEAVY, I_SNOWY_HEAVY, "snow"},
    {86, "Heavy snow showers", I_SNOWY_HEAVY, I_SNOWY_HEAVY, "snow"},
    {95, "Thunderstorm", I_LIGHTNING, I_LIGHTNING, "thunder"},
    {96, "Thunderstorm, hail", I_LIGHTNING_RAINY, I_LIGHTNING_RAINY,
     "thunder"},
    {99, "Thunderstorm, heavy hail", I_LIGHTNING_RAINY, I_LIGHTNING_RAINY,
     "thunder"},
};

static const wmo_entry *wmo_find(int code) {
  for (size_t i = 0; i < ARRAY_LEN(WMO); i++)
    if (WMO[i].code == code)
      return &WMO[i];
  return NULL;
}

const char *wmo_description(int code) {
  const wmo_entry *e = wmo_find(code);
  return e ? e->description : "Unknown";
}

const char *wmo_icon(int code, bool is_day) {
  const wmo_entry *e = wmo_find(code);
  if (!e)
    return I_CLOUDY;
  return is_day ? e->icon_day : e->icon_night;
}

const char *wmo_class(int code) {
  const wmo_entry *e = wmo_find(code);
  return e ? e->css : "unknown";
}

const char *wind_compass(double deg) {
  static const char *const names[] = {"N", "NE", "E", "SE",
                                      "S", "SW", "W", "NW"};
  if (!isfinite(deg))
    return "-";
  /* fmod keeps the sign: shift negative angles into 0..360 first */
  double d = fmod(fmod(deg, 360.0) + 360.0 + 22.5, 360.0);
  int idx = (int)(d / 45.0);
  return names[CLAMP(idx, 0, 7)];
}

const char *wind_arrow(double deg) {
  /* Meteorological direction is where the wind comes FROM,
   * the arrow shows where it blows TO. */
  static const char *const arrows[] = {"↓", "↙", "←", "↖",
                                       "↑", "↗", "→", "↘"};
  if (!isfinite(deg))
    return "";
  double d = fmod(fmod(deg, 360.0) + 360.0 + 22.5, 360.0);
  int idx = (int)(d / 45.0);
  return arrows[CLAMP(idx, 0, 7)];
}

/* ---------- Parsing ---------- */

static double num(const cJSON *obj, const char *key) {
  const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsNumber(it) ? it->valuedouble : NAN;
}

static double arr_num(const cJSON *obj, const char *key, int i) {
  const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
  const cJSON *it = cJSON_GetArrayItem(arr, i);
  return cJSON_IsNumber(it) ? it->valuedouble : NAN;
}

static const char *arr_str(const cJSON *obj, const char *key, int i) {
  const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
  const cJSON *it = cJSON_GetArrayItem(arr, i);
  return cJSON_IsString(it) ? it->valuestring : "";
}

static int to_int(double v) { return isnan(v) ? 0 : (int)lround(v); }

static bool parse_payload(const cJSON *root, weather_t *w, char *err,
                          size_t errlen) {
  const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
  if (cJSON_IsTrue(error)) {
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
    snprintf(err, errlen, "Open-Meteo: %s",
             cJSON_IsString(reason) ? reason->valuestring : "unknown error");
    return false;
  }
  const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
  if (!cJSON_IsObject(cur)) {
    snprintf(err, errlen, "Open-Meteo response has no current weather");
    return false;
  }

  const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "timezone");
  str_copy(w->timezone, cJSON_IsString(tz) ? tz->valuestring : "",
           sizeof w->timezone);
  w->elevation = num(root, "elevation");

  const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "time");
  str_copy(w->time, cJSON_IsString(t) ? t->valuestring : "", sizeof w->time);
  w->temperature = num(cur, "temperature_2m");
  w->apparent_temperature = num(cur, "apparent_temperature");
  w->humidity = num(cur, "relative_humidity_2m");
  w->dew_point = num(cur, "dew_point_2m");
  w->precipitation = num(cur, "precipitation");
  w->cloud_cover = num(cur, "cloud_cover");
  w->pressure_hpa = num(cur, "pressure_msl");
  w->wind_speed = num(cur, "wind_speed_10m");
  w->wind_gusts = num(cur, "wind_gusts_10m");
  w->wind_direction = num(cur, "wind_direction_10m");
  w->uv_index = num(cur, "uv_index");
  w->visibility_m = num(cur, "visibility");
  w->code = to_int(num(cur, "weather_code"));
  w->is_day = to_int(num(cur, "is_day")) != 0;

  if (isnan(w->temperature)) {
    snprintf(err, errlen, "Open-Meteo response is missing the temperature");
    return false;
  }

  /* Hourly: skip entries up to and including the current hour */
  const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
  int nh = cJSON_GetArraySize(
      cJSON_GetObjectItemCaseSensitive(hourly, "time"));
  w->n_hours = 0;
  for (int i = 0; i < nh && w->n_hours < MAX_HOURS; i++) {
    const char *time = arr_str(hourly, "time", i);
    if (strncmp(time, w->time, 13) <= 0)
      continue;
    weather_hour *h = &w->hours[w->n_hours++];
    str_copy(h->time, time, sizeof h->time);
    h->temperature = arr_num(hourly, "temperature_2m", i);
    h->precipitation = arr_num(hourly, "precipitation", i);
    h->precip_probability = to_int(arr_num(hourly, "precipitation_probability", i));
    h->code = to_int(arr_num(hourly, "weather_code", i));
    h->is_day = to_int(arr_num(hourly, "is_day", i)) != 0;
  }

  const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
  int nd = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(daily, "time"));
  w->n_days = 0;
  for (int i = 0; i < nd && w->n_days < MAX_DAYS; i++) {
    weather_day *d = &w->days[w->n_days++];
    str_copy(d->date, arr_str(daily, "time", i), sizeof d->date);
    d->code = to_int(arr_num(daily, "weather_code", i));
    d->temp_max = arr_num(daily, "temperature_2m_max", i);
    d->temp_min = arr_num(daily, "temperature_2m_min", i);
    d->precipitation = arr_num(daily, "precipitation_sum", i);
    d->precip_probability =
        to_int(arr_num(daily, "precipitation_probability_max", i));
    d->uv_max = arr_num(daily, "uv_index_max", i);
    d->wind_max = arr_num(daily, "wind_speed_10m_max", i);
    str_copy(d->sunrise, arr_str(daily, "sunrise", i), sizeof d->sunrise);
    str_copy(d->sunset, arr_str(daily, "sunset", i), sizeof d->sunset);
  }
  return true;
}

/* ---------- Cache ---------- */

static char *cache_file(double lat, double lon, const units_t *u) {
  return xasprintf("%s/weather/%.4f_%.4f_%s_%s_%s.json", xdg_cache_dir(), lat,
                   lon, TEMP_IDS[u->temperature], WIND_IDS[u->wind],
                   PRECIP_IDS[u->precipitation]);
}

/* Returns the cached payload root (caller deletes) and its fetch time */
static cJSON *cache_read(const char *path, time_t *fetched_at) {
  char *text = read_file(path, NULL);
  if (!text)
    return NULL;
  cJSON *root = cJSON_Parse(text);
  free(text);
  const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "fetched_at");
  cJSON *payload = cJSON_DetachItemFromObjectCaseSensitive(root, "payload");
  if (!cJSON_IsNumber(ts) || !cJSON_IsObject(payload)) {
    cJSON_Delete(root);
    cJSON_Delete(payload);
    return NULL;
  }
  *fetched_at = (time_t)ts->valuedouble;
  cJSON_Delete(root);
  return payload;
}

static void cache_write(const char *path, const char *payload) {
  char *text = xasprintf("{\"fetched_at\":%lld,\"payload\":%s}\n",
                         (long long)time(NULL), payload);
  if (!write_file_atomic(path, text, strlen(text)))
    log_warn("Could not write weather cache");
  free(text);
}

/* ---------- Fetching ---------- */

static char *build_url(double lat, double lon, const units_t *u) {
  return xasprintf(
      API_URL "?latitude=%.4f&longitude=%.4f"
              "&current=temperature_2m,relative_humidity_2m,"
              "apparent_temperature,is_day,precipitation,weather_code,"
              "cloud_cover,pressure_msl,wind_speed_10m,wind_direction_10m,"
              "wind_gusts_10m,dew_point_2m,uv_index,visibility"
              "&hourly=temperature_2m,weather_code,precipitation_probability,"
              "precipitation,is_day"
              "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
              "precipitation_sum,precipitation_probability_max,sunrise,"
              "sunset,uv_index_max,wind_speed_10m_max"
              "&timezone=auto&forecast_days=%d&forecast_hours=%d"
              "&temperature_unit=%s&wind_speed_unit=%s&precipitation_unit=%s",
      lat, lon, MAX_DAYS, MAX_HOURS + 2, TEMP_IDS[u->temperature],
      WIND_IDS[u->wind], PRECIP_IDS[u->precipitation]);
}

static bool from_payload(const cJSON *payload, time_t fetched, bool stale,
                         const units_t *units, weather_t *out, char *err,
                         size_t errlen) {
  memset(out, 0, sizeof *out);
  if (!parse_payload(payload, out, err, errlen))
    return false;
  out->fetched_at = fetched;
  out->stale = stale;
  out->units = *units;
  return true;
}

bool weather_get(double lat, double lon, const units_t *units, int ttl,
                 bool force, weather_t *out, char *err, size_t errlen) {
  char *path = cache_file(lat, lon, units);
  time_t fetched = 0;
  cJSON *cached = cache_read(path, &fetched);
  time_t now = time(NULL);
  bool ok = false;

  if (cached && !force && ttl > 0 && now - fetched < ttl) {
    log_debug("Using cached weather (%lds old)", (long)(now - fetched));
    ok = from_payload(cached, fetched, false, units, out, err, errlen);
    if (ok)
      goto done;
  }

  char *url = build_url(lat, lon, units);
  http_response res;
  char http_err[256] = "";
  bool fetched_ok = http_get(url, HTTP_TIMEOUT, &res, http_err,
                             sizeof http_err);
  free(url);

  cJSON *payload = res.body ? cJSON_Parse(res.body) : NULL;
  if (payload) {
    ok = from_payload(payload, now, false, units, out, err, errlen);
    if (ok && fetched_ok)
      cache_write(path, res.body);
    cJSON_Delete(payload);
  } else {
    snprintf(err, errlen, "Weather request failed: %s",
             http_err[0] ? http_err : "invalid response");
  }
  http_response_free(&res);

  if (!ok && cached) {
    log_warn("%s; using cached data from %lds ago", err,
             (long)(now - fetched));
    ok = from_payload(cached, fetched, true, units, out, err, errlen);
  }

done:
  cJSON_Delete(cached);
  free(path);
  return ok;
}
