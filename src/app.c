#include "app.h"
#include "geolocate.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

bool app_location_set(const location_t *loc) {
  return !str_empty(loc->city) || !str_empty(loc->name) ||
         loc->latitude != 0 || loc->longitude != 0;
}

bool app_resolve(const config_t *cfg, const char *ref, bool force,
                 location_t *out, char *source, size_t source_len, char *err,
                 size_t errlen) {
  if (!str_empty(ref)) {
    int idx = config_find(cfg, ref);
    if (idx == -2) {
      snprintf(err, errlen, "\"%s\" matches several saved locations", ref);
      return false;
    }
    if (idx < 0) {
      snprintf(err, errlen, "No saved location matches \"%s\"", ref);
      return false;
    }
    *out = cfg->locations[idx];
    snprintf(source, source_len, "saved #%d", idx + 1);
    return true;
  }

  if (cfg->geo.enabled) {
    location_t loc = cfg->current;
    loc.name[0] = '\0';
    char geo_source[64], geo_err[512];
    if (geolocate(&cfg->geo, force, &loc, geo_source, sizeof geo_source,
                  geo_err, sizeof geo_err)) {
      *out = loc;
      snprintf(source, source_len, "%s", geo_source);
      return true;
    }
    if (app_location_set(&cfg->current)) {
      log_warn("Geolocation failed (%s), using the configured location",
               geo_err);
      *out = cfg->current;
      snprintf(source, source_len, "config (geolocation failed)");
      return true;
    }
    snprintf(err, errlen, "Geolocation failed: %s", geo_err);
    return false;
  }

  if (!app_location_set(&cfg->current)) {
    snprintf(err, errlen,
             "No location configured. Run \"wayweather tui\", "
             "\"wayweather add <city>\" or \"wayweather geo on\"");
    return false;
  }
  *out = cfg->current;
  int match = config_match_current(cfg);
  if (match >= 0)
    snprintf(source, source_len, "saved #%d", match + 1);
  else
    snprintf(source, source_len, "config");
  return true;
}

bool app_fetch(const config_t *cfg, const char *ref, bool force, int ttl,
               location_t *loc, weather_t *w, char *source, size_t source_len,
               char *err, size_t errlen) {
  if (!app_resolve(cfg, ref, force, loc, source, source_len, err, errlen))
    return false;
  return weather_get(loc->latitude, loc->longitude, &loc->units, ttl, force,
                     w, err, errlen);
}

bool app_cycle(config_t *cfg, int direction, char *desc, size_t desc_len) {
  int n = cfg->n_locations;
  bool geo_slot = cfg->geo.in_cycle;
  int slots = n + (geo_slot ? 1 : 0); /* slot n == geolocation */
  if (slots == 0 || (slots == 1 && !geo_slot && cfg->active == 0)) {
    snprintf(desc, desc_len, "No other locations to switch to");
    return false;
  }

  int cur;
  if (cfg->geo.enabled)
    cur = geo_slot ? n : -1;
  else
    cur = config_match_current(cfg);

  int next;
  if (cur < 0)
    next = direction > 0 ? 0 : slots - 1;
  else
    next = ((cur + direction) % slots + slots) % slots;

  if (next == n) {
    cfg->geo.enabled = true;
    cfg->active = -1;
    snprintf(desc, desc_len, "Geolocation");
  } else {
    config_use(cfg, next);
    char label[FIELD_LEN];
    location_label(&cfg->locations[next], label, sizeof label);
    snprintf(desc, desc_len, "%s", label);
  }
  return true;
}
