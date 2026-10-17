#define _GNU_SOURCE
#include "geocode.h"
#include "http.h"
#include "log.h"
#include "util.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEARCH_URL "https://geocoding-api.open-meteo.com/v1/search"
#define REVERSE_URL "https://nominatim.openstreetmap.org/reverse"
#define FETCH_LIMIT 30

static const char *jstr(const cJSON *o, const char *key) {
  const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(it) ? it->valuestring : "";
}

static double jnum(const cJSON *o, const char *key) {
  const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(it) ? it->valuedouble : 0;
}

static bool matches_filter(const cJSON *r, const char *filter) {
  if (str_empty(filter))
    return true;
  return str_icontains(jstr(r, "admin1"), filter) ||
         str_icontains(jstr(r, "admin2"), filter) ||
         str_icontains(jstr(r, "country"), filter) ||
         str_ieq(jstr(r, "country_code"), filter);
}

int geocode_search(const char *query, int max, geocode_result *out,
                   char *err, size_t errlen) {
  char *q = xstrdup(query);
  char *filter = strchr(q, ',');
  if (filter) {
    *filter++ = '\0';
    filter = str_trim(filter);
  }
  char *name = str_trim(q);
  if (strlen(name) < 2) {
    snprintf(err, errlen, "Search needs at least 2 characters");
    free(q);
    return -1;
  }

  char *esc = http_escape(name);
  char *url = xasprintf(SEARCH_URL "?name=%s&count=%d&language=en&format=json",
                        esc, str_empty(filter) ? max : FETCH_LIMIT);
  free(esc);

  http_response res;
  char http_err[256] = "";
  bool ok = http_get(url, 10, &res, http_err, sizeof http_err);
  free(url);
  if (!ok) {
    snprintf(err, errlen, "Place search failed: %s", http_err);
    http_response_free(&res);
    free(q);
    return -1;
  }

  cJSON *root = cJSON_Parse(res.body);
  http_response_free(&res);
  if (!root) {
    snprintf(err, errlen, "Place search returned invalid data");
    free(q);
    return -1;
  }

  int n = 0;
  const cJSON *r;
  cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(root, "results")) {
    if (n >= max)
      break;
    if (!matches_filter(r, filter))
      continue;
    geocode_result *g = &out[n++];
    memset(g, 0, sizeof *g);
    location_init(&g->loc);
    str_copy(g->loc.city, jstr(r, "name"), sizeof g->loc.city);
    str_copy(g->loc.region, jstr(r, "admin1"), sizeof g->loc.region);
    str_copy(g->loc.country, jstr(r, "country"), sizeof g->loc.country);
    g->loc.latitude = jnum(r, "latitude");
    g->loc.longitude = jnum(r, "longitude");
    str_copy(g->country_code, jstr(r, "country_code"), sizeof g->country_code);
    g->population = (long)jnum(r, "population");
    g->elevation = jnum(r, "elevation");
    str_copy(g->timezone, jstr(r, "timezone"), sizeof g->timezone);
    /* The US and Liberia mostly use imperial units */
    if (str_ieq(g->country_code, "US") || str_ieq(g->country_code, "LR"))
      units_imperial(&g->loc.units);
  }
  cJSON_Delete(root);
  free(q);
  if (n == 0)
    snprintf(err, errlen, "No places found for \"%s\"", query);
  return n;
}

/* ---------- Reverse geocoding ---------- */

static char *reverse_cache_path(double lat, double lon) {
  return xasprintf("%s/places/%.2f_%.2f.json", xdg_cache_dir(), lat, lon);
}

static bool fill_from_address(const cJSON *addr, location_t *loc) {
  static const char *const city_keys[] = {
      "city", "town", "village", "municipality", "hamlet", "suburb", "county"};
  const char *city = "";
  for (size_t i = 0; i < ARRAY_LEN(city_keys) && str_empty(city); i++)
    city = jstr(addr, city_keys[i]);
  const char *region = jstr(addr, "state");
  if (str_empty(region))
    region = jstr(addr, "region");
  const char *country = jstr(addr, "country");
  if (str_empty(city) && str_empty(country))
    return false;
  str_copy(loc->city, city, sizeof loc->city);
  str_copy(loc->region, region, sizeof loc->region);
  str_copy(loc->country, country, sizeof loc->country);
  return true;
}

bool geocode_reverse(double lat, double lon, location_t *loc) {
  /* Places don't move: the cache never expires (~1 km granularity) */
  char *path = reverse_cache_path(lat, lon);
  char *text = read_file(path, NULL);
  if (text) {
    cJSON *addr = cJSON_Parse(text);
    free(text);
    bool ok = addr && fill_from_address(addr, loc);
    cJSON_Delete(addr);
    if (ok) {
      free(path);
      return true;
    }
  }

  char *url = xasprintf(REVERSE_URL "?format=jsonv2&lat=%.5f&lon=%.5f"
                                    "&zoom=10&accept-language=en",
                        lat, lon);
  http_response res;
  char err[256];
  bool ok = http_get(url, 8, &res, err, sizeof err);
  free(url);
  if (!ok) {
    log_warn("Reverse geocoding failed: %s", err);
    http_response_free(&res);
    free(path);
    return false;
  }

  cJSON *root = cJSON_Parse(res.body);
  http_response_free(&res);
  const cJSON *addr = cJSON_GetObjectItemCaseSensitive(root, "address");
  ok = cJSON_IsObject(addr) && fill_from_address(addr, loc);
  if (ok) {
    char *s = cJSON_PrintUnformatted(addr);
    write_file_atomic(path, s, strlen(s));
    cJSON_free(s);
  }
  cJSON_Delete(root);
  free(path);
  return ok;
}
