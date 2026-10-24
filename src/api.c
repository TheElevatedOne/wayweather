/* Public libwayweather API (wayweather.h) on top of the internal modules */

#define _GNU_SOURCE
#include "wayweather.h"
#include "app.h"
#include "config.h"
#include "geocode.h"
#include "geolocate.h"
#include "http.h"
#include "log.h"
#include "render.h"
#include "units.h"
#include "util.h"
#include "version.h"
#include "weather.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Locale guard ---------- */

/* The internals format URLs and parse numbers with printf/strtod, which
 * follow LC_NUMERIC. Every entry point switches the calling thread to a
 * locale with the "C" numeric category and restores the caller's after. */
static locale_t c_numeric = (locale_t)0;

#define LOCALE_ENTER locale_t ww_prev_locale_ = enter_c_numeric()
#define LOCALE_LEAVE leave_c_numeric(ww_prev_locale_)

static locale_t enter_c_numeric(void) {
  return c_numeric ? uselocale(c_numeric) : (locale_t)0;
}

static void leave_c_numeric(locale_t prev) {
  if (prev)
    uselocale(prev);
}

static void set_err(char *err, size_t errlen, const char *msg) {
  if (err && errlen)
    str_copy(err, msg, errlen);
}

/* ---------- General ---------- */

const char *ww_version(void) { return WW_VERSION; }

int ww_init(void) {
  log_level = -1; /* silent unless the program asks for messages */
  http_init();
  /* Resolve the lazily cached paths before threads exist */
  xdg_config_dir();
  xdg_cache_dir();
  xdg_runtime_dir();
  if (!c_numeric) {
    locale_t base = duplocale(LC_GLOBAL_LOCALE);
    if (base) {
      c_numeric = newlocale(LC_NUMERIC_MASK, "C", base);
      if (!c_numeric)
        freelocale(base);
    }
  }
  return 0;
}

void ww_cleanup(void) {
  http_cleanup();
  if (c_numeric) {
    freelocale(c_numeric);
    c_numeric = (locale_t)0;
  }
}

void ww_set_verbosity(int level) {
  log_level = CLAMP(level, 0, 3) - 1;
}

void ww_set_config_path(const char *path) {
  config_set_path(path ? path : "");
}

/* ---------- Units ---------- */

static void units_in(const ww_units *in, units_t *out) {
  if (!in) {
    units_metric(out);
    return;
  }
  out->temperature = (temp_unit)CLAMP((int)in->temperature, 0, TEMP_COUNT - 1);
  out->wind = (wind_unit)CLAMP((int)in->wind, 0, WIND_COUNT - 1);
  out->precipitation =
      (precip_unit)CLAMP((int)in->precipitation, 0, PRECIP_COUNT - 1);
  out->pressure =
      (pressure_unit)CLAMP((int)in->pressure, 0, PRESSURE_COUNT - 1);
  out->clock = (clock_format)CLAMP((int)in->clock, 0, CLOCK_COUNT - 1);
}

static void units_out(const units_t *in, ww_units *out) {
  out->temperature = (ww_temp_unit)in->temperature;
  out->wind = (ww_wind_unit)in->wind;
  out->precipitation = (ww_precip_unit)in->precipitation;
  out->pressure = (ww_pressure_unit)in->pressure;
  out->clock = (ww_clock)in->clock;
}

void ww_units_metric(ww_units *u) {
  units_t m;
  units_metric(&m);
  units_out(&m, u);
}

void ww_units_imperial(ww_units *u) {
  units_t i;
  units_imperial(&i);
  units_out(&i, u);
}

#define LABEL(table, count, v)                                                 \
  ((int)(v) >= 0 && (int)(v) < (count) ? (table)[(int)(v)] : "")

const char *ww_temp_label(ww_temp_unit u) {
  return LABEL(TEMP_LABELS, TEMP_COUNT, u);
}
const char *ww_wind_label(ww_wind_unit u) {
  return LABEL(WIND_LABELS, WIND_COUNT, u);
}
const char *ww_precip_label(ww_precip_unit u) {
  return LABEL(PRECIP_LABELS, PRECIP_COUNT, u);
}
const char *ww_pressure_label(ww_pressure_unit u) {
  return LABEL(PRESSURE_LABELS, PRESSURE_COUNT, u);
}

double ww_pressure_from_hpa(double hpa, ww_pressure_unit u) {
  return pressure_from_hpa(hpa,
                           (pressure_unit)CLAMP((int)u, 0, PRESSURE_COUNT - 1));
}

/* ---------- Locations ---------- */

static void location_in(const ww_location *in, location_t *out) {
  location_init(out);
  str_copy(out->name, in->name, sizeof out->name);
  str_copy(out->city, in->city, sizeof out->city);
  str_copy(out->region, in->region, sizeof out->region);
  str_copy(out->country, in->country, sizeof out->country);
  out->latitude = in->latitude;
  out->longitude = in->longitude;
  units_in(&in->units, &out->units);
  out->is_default = in->is_default != 0;
}

static void location_out(const location_t *in, ww_location *out) {
  memset(out, 0, sizeof *out);
  str_copy(out->name, in->name, sizeof out->name);
  str_copy(out->city, in->city, sizeof out->city);
  str_copy(out->region, in->region, sizeof out->region);
  str_copy(out->country, in->country, sizeof out->country);
  out->latitude = in->latitude;
  out->longitude = in->longitude;
  units_out(&in->units, &out->units);
  out->is_default = in->is_default;
}

int ww_geocode(const char *query, ww_location *out, int max, char *err,
               size_t errlen) {
  if (!query || !out || max <= 0) {
    set_err(err, errlen, "invalid arguments");
    return -1;
  }
  LOCALE_ENTER;
  geocode_result *res = xcalloc((size_t)max, sizeof *res);
  char e[256] = "";
  int n = geocode_search(query, max, res, e, sizeof e);
  for (int i = 0; i < n; i++)
    location_out(&res[i].loc, &out[i]);
  free(res);
  if (n < 0)
    set_err(err, errlen, e);
  else if (n == 0)
    set_err(err, errlen, e); /* "No places found ..." */
  LOCALE_LEAVE;
  return n;
}

int ww_reverse_geocode(double lat, double lon, ww_location *loc) {
  if (!loc)
    return -1;
  LOCALE_ENTER;
  location_t l;
  location_in(loc, &l);
  bool ok = geocode_reverse(lat, lon, &l);
  if (ok) {
    str_copy(loc->city, l.city, sizeof loc->city);
    str_copy(loc->region, l.region, sizeof loc->region);
    str_copy(loc->country, l.country, sizeof loc->country);
  }
  LOCALE_LEAVE;
  return ok ? 0 : -1;
}

int ww_geolocate(ww_geo_provider provider, int timeout, int cache_ttl,
                 ww_location *out, char *source, size_t source_len, char *err,
                 size_t errlen) {
  if (!out) {
    set_err(err, errlen, "invalid arguments");
    return -1;
  }
  LOCALE_ENTER;
  config_t defaults;
  config_defaults(&defaults);
  geo_config geo = defaults.geo;
  geo.provider = (geo_provider)CLAMP((int)provider, 0, GEO_PROVIDER_COUNT - 1);
  geo.timeout = CLAMP(timeout, 1, 60);
  geo.cache_ttl = cache_ttl > 0 ? cache_ttl : 0;

  location_t loc;
  location_in(out, &loc);
  char src[64] = "", e[512] = "";
  bool ok = geolocate(&geo, cache_ttl <= 0, &loc, src, sizeof src, e, sizeof e);
  if (ok) {
    out->latitude = loc.latitude;
    out->longitude = loc.longitude;
    str_copy(out->city, loc.city, sizeof out->city);
    str_copy(out->region, loc.region, sizeof out->region);
    str_copy(out->country, loc.country, sizeof out->country);
    if (source && source_len)
      str_copy(source, src, source_len);
  } else {
    set_err(err, errlen, e);
  }
  LOCALE_LEAVE;
  return ok ? 0 : -1;
}

static bool load_user_config(config_t *cfg, char *err, size_t errlen) {
  if (config_load(cfg))
    return true;
  config_free(cfg);
  set_err(err, errlen, "the wayweather config file is invalid");
  return false;
}

int ww_current_location(ww_location *out, char *err, size_t errlen) {
  if (!out) {
    set_err(err, errlen, "invalid arguments");
    return -1;
  }
  LOCALE_ENTER;
  config_t cfg;
  int rc = -1;
  if (load_user_config(&cfg, err, errlen)) {
    location_t loc;
    char source[96], e[512] = "";
    if (app_resolve(&cfg, NULL, false, &loc, source, sizeof source, e,
                    sizeof e)) {
      location_out(&loc, out);
      rc = 0;
    } else {
      set_err(err, errlen, e);
    }
    config_free(&cfg);
  }
  LOCALE_LEAVE;
  return rc;
}

int ww_saved_locations(ww_location **out, char *err, size_t errlen) {
  if (!out) {
    set_err(err, errlen, "invalid arguments");
    return -1;
  }
  *out = NULL;
  LOCALE_ENTER;
  config_t cfg;
  int n = -1;
  if (load_user_config(&cfg, err, errlen)) {
    n = cfg.n_locations;
    if (n > 0) {
      *out = malloc(sizeof **out * (size_t)n);
      if (!*out) {
        set_err(err, errlen, "out of memory");
        n = -1;
      } else {
        for (int i = 0; i < n; i++)
          location_out(&cfg.locations[i], &(*out)[i]);
      }
    }
    config_free(&cfg);
  }
  LOCALE_LEAVE;
  return n;
}

/* ---------- Weather ---------- */

static void weather_out(const weather_t *in, ww_weather *out) {
  memset(out, 0, sizeof *out);
  out->fetched_at = (long long)in->fetched_at;
  out->stale = in->stale;
  str_copy(out->timezone, in->timezone, sizeof out->timezone);
  str_copy(out->time, in->time, sizeof out->time);
  out->elevation = in->elevation;
  out->temperature = in->temperature;
  out->apparent_temperature = in->apparent_temperature;
  out->humidity = in->humidity;
  out->dew_point = in->dew_point;
  out->precipitation = in->precipitation;
  out->cloud_cover = in->cloud_cover;
  out->pressure_hpa = in->pressure_hpa;
  out->wind_speed = in->wind_speed;
  out->wind_gusts = in->wind_gusts;
  out->wind_direction = in->wind_direction;
  out->uv_index = in->uv_index;
  out->visibility_m = in->visibility_m;
  out->code = in->code;
  out->is_day = in->is_day;
  out->n_hours = CLAMP(in->n_hours, 0, WW_MAX_HOURS);
  for (int i = 0; i < out->n_hours; i++) {
    const weather_hour *h = &in->hours[i];
    ww_hour *o = &out->hours[i];
    str_copy(o->time, h->time, sizeof o->time);
    o->temperature = h->temperature;
    o->precipitation = h->precipitation;
    o->precip_probability = h->precip_probability;
    o->code = h->code;
    o->is_day = h->is_day;
  }
  out->n_days = CLAMP(in->n_days, 0, WW_MAX_DAYS);
  for (int i = 0; i < out->n_days; i++) {
    const weather_day *d = &in->days[i];
    ww_day *o = &out->days[i];
    str_copy(o->date, d->date, sizeof o->date);
    o->code = d->code;
    o->temp_max = d->temp_max;
    o->temp_min = d->temp_min;
    o->precipitation = d->precipitation;
    o->precip_probability = d->precip_probability;
    o->uv_max = d->uv_max;
    o->wind_max = d->wind_max;
    str_copy(o->sunrise, d->sunrise, sizeof o->sunrise);
    str_copy(o->sunset, d->sunset, sizeof o->sunset);
  }
  units_out(&in->units, &out->units);
}

static void weather_in(const ww_weather *in, weather_t *out) {
  memset(out, 0, sizeof *out);
  out->fetched_at = (time_t)in->fetched_at;
  out->stale = in->stale != 0;
  str_copy(out->timezone, in->timezone, sizeof out->timezone);
  str_copy(out->time, in->time, sizeof out->time);
  out->elevation = in->elevation;
  out->temperature = in->temperature;
  out->apparent_temperature = in->apparent_temperature;
  out->humidity = in->humidity;
  out->dew_point = in->dew_point;
  out->precipitation = in->precipitation;
  out->cloud_cover = in->cloud_cover;
  out->pressure_hpa = in->pressure_hpa;
  out->wind_speed = in->wind_speed;
  out->wind_gusts = in->wind_gusts;
  out->wind_direction = in->wind_direction;
  out->uv_index = in->uv_index;
  out->visibility_m = in->visibility_m;
  out->code = in->code;
  out->is_day = in->is_day != 0;
  out->n_hours = CLAMP(in->n_hours, 0, MAX_HOURS);
  for (int i = 0; i < out->n_hours; i++) {
    const ww_hour *h = &in->hours[i];
    weather_hour *o = &out->hours[i];
    str_copy(o->time, h->time, sizeof o->time);
    o->temperature = h->temperature;
    o->precipitation = h->precipitation;
    o->precip_probability = h->precip_probability;
    o->code = h->code;
    o->is_day = h->is_day != 0;
  }
  out->n_days = CLAMP(in->n_days, 0, MAX_DAYS);
  for (int i = 0; i < out->n_days; i++) {
    const ww_day *d = &in->days[i];
    weather_day *o = &out->days[i];
    str_copy(o->date, d->date, sizeof o->date);
    o->code = d->code;
    o->temp_max = d->temp_max;
    o->temp_min = d->temp_min;
    o->precipitation = d->precipitation;
    o->precip_probability = d->precip_probability;
    o->uv_max = d->uv_max;
    o->wind_max = d->wind_max;
    str_copy(o->sunrise, d->sunrise, sizeof o->sunrise);
    str_copy(o->sunset, d->sunset, sizeof o->sunset);
  }
  units_in(&in->units, &out->units);
}

int ww_weather_get(double lat, double lon, const ww_units *units,
                   int cache_ttl, ww_weather *out, char *err, size_t errlen) {
  if (!out) {
    set_err(err, errlen, "invalid arguments");
    return -1;
  }
  LOCALE_ENTER;
  units_t u;
  units_in(units, &u);
  weather_t *w = xmalloc(sizeof *w);
  char e[512] = "";
  int ttl = cache_ttl > 0 ? cache_ttl : 0;
  bool ok = weather_get(lat, lon, &u, ttl, ttl == 0, w, e, sizeof e);
  if (ok)
    weather_out(w, out);
  else
    set_err(err, errlen, e);
  free(w);
  LOCALE_LEAVE;
  return ok ? 0 : -1;
}

int ww_weather_for(const ww_location *loc, int cache_ttl, ww_weather *out,
                   char *err, size_t errlen) {
  if (!loc) {
    set_err(err, errlen, "invalid arguments");
    return -1;
  }
  return ww_weather_get(loc->latitude, loc->longitude, &loc->units, cache_ttl,
                        out, err, errlen);
}

const char *ww_wmo_description(int code) { return wmo_description(code); }
const char *ww_wmo_icon(int code, int is_day) {
  return wmo_icon(code, is_day != 0);
}
const char *ww_wmo_class(int code) { return wmo_class(code); }
const char *ww_wind_compass(double degrees) { return wind_compass(degrees); }
const char *ww_wind_arrow(double degrees) { return wind_arrow(degrees); }

/* ---------- Formatting ---------- */

char *ww_format(const char *tmpl, const ww_location *loc,
                const ww_weather *weather, int decimals, int icons) {
  if (!tmpl || !loc || !weather)
    return NULL;
  LOCALE_ENTER;
  config_t cfg;
  config_defaults(&cfg);
  cfg.display.decimals = CLAMP(decimals, 0, 2);
  cfg.display.icons = icons != 0;
  location_t l;
  location_in(loc, &l);
  weather_t *w = xmalloc(sizeof *w);
  weather_in(weather, w);
  render_ctx ctx = {&cfg, &l, w, "", icons != 0};
  char *out = render_template(&ctx, tmpl);
  free(w);
  LOCALE_LEAVE;
  return out;
}

char *ww_waybar_json(const char *location) {
  LOCALE_ENTER;
  config_t cfg;
  char *out;
  if (!config_load(&cfg)) {
    config_free(&cfg);
    out = render_waybar_error("?", "The wayweather config file is invalid");
  } else {
    location_t loc;
    weather_t *w = xmalloc(sizeof *w);
    char source[96], err[512];
    if (app_fetch(&cfg, location, false, cfg.cache_ttl, &loc, w, source,
                  sizeof source, err, sizeof err)) {
      render_ctx ctx = {&cfg, &loc, w, source, cfg.display.icons};
      out = render_waybar(&ctx);
    } else {
      out = render_waybar_error(cfg.display.icons ? "\U000F0F2F" : "?", err);
    }
    free(w);
    config_free(&cfg);
  }
  LOCALE_LEAVE;
  return out;
}
