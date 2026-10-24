#define _GNU_SOURCE
#include "config.h"
#include "log.h"
#include "util.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

const char *const GEO_PROVIDER_IDS[GEO_PROVIDER_COUNT] = {"auto", "gpsd",
                                                          "geoclue", "ip"};

static char custom_path[PATH_MAX];

void config_set_path(const char *path) {
  str_copy(custom_path, path, sizeof custom_path);
}

const char *config_path(void) {
  static char path[PATH_MAX];
  if (custom_path[0])
    return custom_path;
  if (!path[0])
    snprintf(path, sizeof path, "%s/config.json", xdg_config_dir());
  return path;
}

/* ---------- Defaults ---------- */

void location_init(location_t *loc) {
  memset(loc, 0, sizeof *loc);
  units_metric(&loc->units);
}

void config_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  location_init(&cfg->current);
  cfg->active = -1;

  cfg->geo.enabled = true;
  cfg->geo.provider = GEO_AUTO;
  str_copy(cfg->geo.gpsd_host, "localhost", sizeof cfg->geo.gpsd_host);
  cfg->geo.gpsd_port = 2947;
  cfg->geo.timeout = 5;
  cfg->geo.cache_ttl = 600;
  cfg->geo.reverse_geocode = true;
  cfg->geo.in_cycle = true;

  cfg->display.icons = true;
  str_copy(cfg->display.text, "{temp}{tunit} {icon}",
           sizeof cfg->display.text);
  str_copy(cfg->display.tooltip_title, "{place}",
           sizeof cfg->display.tooltip_title);
  cfg->display.tooltip = true;
  cfg->display.hours = 6;
  cfg->display.days = 5;
  cfg->display.decimals = 1;

  cfg->refresh_interval = 900;
  cfg->cache_ttl = 600;
}

void config_free(config_t *cfg) {
  free(cfg->locations);
  cfg->locations = NULL;
  cfg->n_locations = cfg->cap_locations = 0;
}

/* ---------- JSON helpers ---------- */

static const cJSON *jget(const cJSON *obj, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(obj, key);
}

static void jstr(const cJSON *obj, const char *key, char *dst, size_t size) {
  const cJSON *it = jget(obj, key);
  if (cJSON_IsString(it))
    str_copy(dst, it->valuestring, size);
}

static void jnum(const cJSON *obj, const char *key, double *dst) {
  const cJSON *it = jget(obj, key);
  if (cJSON_IsNumber(it))
    *dst = it->valuedouble;
  else if (cJSON_IsString(it))
    parse_double(it->valuestring, dst);
}

static void jint(const cJSON *obj, const char *key, int *dst, int lo, int hi) {
  double v = *dst;
  jnum(obj, key, &v);
  *dst = (int)CLAMP(v, lo, hi);
}

static void jbool(const cJSON *obj, const char *key, bool *dst) {
  const cJSON *it = jget(obj, key);
  if (cJSON_IsBool(it))
    *dst = cJSON_IsTrue(it);
  else if (cJSON_IsString(it))
    *dst = str_ieq(it->valuestring, "true") || str_ieq(it->valuestring, "yes");
}

static void jenum(const cJSON *obj, const char *key, const char *const *ids,
                  int count, int *dst) {
  const cJSON *it = jget(obj, key);
  if (!cJSON_IsString(it))
    return;
  int v = unit_lookup(ids, count, it->valuestring);
  if (v >= 0)
    *dst = v;
  else
    log_warn("Config: unknown value \"%s\" for \"%s\"", it->valuestring, key);
}

static void parse_units(const cJSON *it, units_t *u) {
  /* Legacy formats: "met"/"imp" strings, or true = metric */
  if (cJSON_IsString(it)) {
    if (!units_from_preset(it->valuestring, u))
      log_warn("Config: unknown units preset \"%s\"", it->valuestring);
    return;
  }
  if (cJSON_IsBool(it)) {
    if (cJSON_IsTrue(it))
      units_metric(u);
    else
      units_imperial(u);
    return;
  }
  if (!cJSON_IsObject(it))
    return;
  const cJSON *preset = jget(it, "preset");
  if (cJSON_IsString(preset))
    units_from_preset(preset->valuestring, u);
  int v;
  v = u->temperature;
  jenum(it, "temperature", TEMP_IDS, TEMP_COUNT, &v);
  u->temperature = v;
  v = u->wind;
  jenum(it, "wind_speed", WIND_IDS, WIND_COUNT, &v);
  u->wind = v;
  v = u->precipitation;
  jenum(it, "precipitation", PRECIP_IDS, PRECIP_COUNT, &v);
  u->precipitation = v;
  v = u->pressure;
  jenum(it, "pressure", PRESSURE_IDS, PRESSURE_COUNT, &v);
  u->pressure = v;
  v = u->clock;
  jenum(it, "time_format", CLOCK_IDS, CLOCK_COUNT, &v);
  u->clock = v;
}

static void parse_location(const cJSON *obj, location_t *loc) {
  jstr(obj, "name", loc->name, sizeof loc->name);
  jstr(obj, "city", loc->city, sizeof loc->city);
  jstr(obj, "region", loc->region, sizeof loc->region);
  jstr(obj, "country", loc->country, sizeof loc->country);
  jnum(obj, "latitude", &loc->latitude);
  jnum(obj, "longitude", &loc->longitude);
  jbool(obj, "default", &loc->is_default);
  parse_units(jget(obj, "units"), &loc->units);
}

static cJSON *units_to_json(const units_t *u) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "temperature", TEMP_IDS[u->temperature]);
  cJSON_AddStringToObject(o, "wind_speed", WIND_IDS[u->wind]);
  cJSON_AddStringToObject(o, "precipitation", PRECIP_IDS[u->precipitation]);
  cJSON_AddStringToObject(o, "pressure", PRESSURE_IDS[u->pressure]);
  cJSON_AddStringToObject(o, "time_format", CLOCK_IDS[u->clock]);
  return o;
}

static double round_coord(double v) { return round(v * 1e5) / 1e5; }

static cJSON *location_to_json(const location_t *loc, bool with_default) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "name", loc->name);
  cJSON_AddStringToObject(o, "city", loc->city);
  cJSON_AddStringToObject(o, "region", loc->region);
  cJSON_AddStringToObject(o, "country", loc->country);
  cJSON_AddNumberToObject(o, "latitude", round_coord(loc->latitude));
  cJSON_AddNumberToObject(o, "longitude", round_coord(loc->longitude));
  cJSON_AddItemToObject(o, "units", units_to_json(&loc->units));
  if (with_default)
    cJSON_AddBoolToObject(o, "default", loc->is_default);
  return o;
}

char *config_to_json(const config_t *cfg) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "version", CONFIG_VERSION);
  cJSON_AddItemToObject(root, "current",
                        location_to_json(&cfg->current, false));
  cJSON_AddNumberToObject(root, "active", cfg->active);

  cJSON *geo = cJSON_AddObjectToObject(root, "geolocation");
  cJSON_AddBoolToObject(geo, "enabled", cfg->geo.enabled);
  cJSON_AddStringToObject(geo, "provider",
                          GEO_PROVIDER_IDS[cfg->geo.provider]);
  cJSON_AddStringToObject(geo, "gpsd_host", cfg->geo.gpsd_host);
  cJSON_AddNumberToObject(geo, "gpsd_port", cfg->geo.gpsd_port);
  cJSON_AddNumberToObject(geo, "timeout", cfg->geo.timeout);
  cJSON_AddNumberToObject(geo, "cache_ttl", cfg->geo.cache_ttl);
  cJSON_AddBoolToObject(geo, "reverse_geocode", cfg->geo.reverse_geocode);
  cJSON_AddBoolToObject(geo, "include_in_cycle", cfg->geo.in_cycle);

  cJSON *disp = cJSON_AddObjectToObject(root, "display");
  cJSON_AddBoolToObject(disp, "icons", cfg->display.icons);
  cJSON_AddStringToObject(disp, "text", cfg->display.text);
  cJSON_AddStringToObject(disp, "tooltip_title", cfg->display.tooltip_title);
  cJSON_AddBoolToObject(disp, "tooltip", cfg->display.tooltip);
  cJSON_AddNumberToObject(disp, "hours", cfg->display.hours);
  cJSON_AddNumberToObject(disp, "days", cfg->display.days);
  cJSON_AddNumberToObject(disp, "decimals", cfg->display.decimals);

  cJSON_AddNumberToObject(root, "refresh_interval", cfg->refresh_interval);
  cJSON_AddNumberToObject(root, "cache_ttl", cfg->cache_ttl);

  cJSON *locs = cJSON_AddArrayToObject(root, "locations");
  for (int i = 0; i < cfg->n_locations; i++)
    cJSON_AddItemToArray(locs, location_to_json(&cfg->locations[i], true));

  char *out = cJSON_Print(root);
  cJSON_Delete(root);
  return out;
}

char *config_locations_json(const config_t *cfg) {
  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < cfg->n_locations; i++) {
    cJSON *o = location_to_json(&cfg->locations[i], true);
    cJSON_AddNumberToObject(o, "id", i + 1);
    cJSON_AddBoolToObject(o, "active", !cfg->geo.enabled &&
                                           config_match_current(cfg) == i);
    cJSON_AddItemToArray(arr, o);
  }
  char *out = cJSON_Print(arr);
  cJSON_Delete(arr);
  return out;
}

static void parse_v3(const cJSON *root, config_t *cfg) {
  parse_location(jget(root, "current"), &cfg->current);
  jint(root, "active", &cfg->active, -1, MAX_LOCATIONS);

  const cJSON *geo = jget(root, "geolocation");
  jbool(geo, "enabled", &cfg->geo.enabled);
  int p = cfg->geo.provider;
  jenum(geo, "provider", GEO_PROVIDER_IDS, GEO_PROVIDER_COUNT, &p);
  cfg->geo.provider = p;
  jstr(geo, "gpsd_host", cfg->geo.gpsd_host, sizeof cfg->geo.gpsd_host);
  jint(geo, "gpsd_port", &cfg->geo.gpsd_port, 1, 65535);
  jint(geo, "timeout", &cfg->geo.timeout, 1, 60);
  jint(geo, "cache_ttl", &cfg->geo.cache_ttl, 0, 86400);
  jbool(geo, "reverse_geocode", &cfg->geo.reverse_geocode);
  jbool(geo, "include_in_cycle", &cfg->geo.in_cycle);

  const cJSON *disp = jget(root, "display");
  jbool(disp, "icons", &cfg->display.icons);
  jstr(disp, "text", cfg->display.text, sizeof cfg->display.text);
  jstr(disp, "tooltip_title", cfg->display.tooltip_title,
       sizeof cfg->display.tooltip_title);
  jbool(disp, "tooltip", &cfg->display.tooltip);
  jint(disp, "hours", &cfg->display.hours, 0, 24);
  jint(disp, "days", &cfg->display.days, 0, 7);
  jint(disp, "decimals", &cfg->display.decimals, 0, 2);

  jint(root, "refresh_interval", &cfg->refresh_interval, 60, 86400);
  jint(root, "cache_ttl", &cfg->cache_ttl, 0, 86400);

  const cJSON *locs = jget(root, "locations");
  const cJSON *it;
  cJSON_ArrayForEach(it, locs) {
    location_t loc;
    location_init(&loc);
    parse_location(it, &loc);
    config_add(cfg, &loc);
  }
  if (cfg->active >= cfg->n_locations)
    cfg->active = -1;
}

/* ---------- Legacy migration ---------- */

/* Config written by the unfinished C rewrite (no "version" key) */
static void parse_legacy_json(const cJSON *root, config_t *cfg) {
  jbool(root, "geolocation", &cfg->geo.enabled);
  parse_units(jget(root, "units"), &cfg->current.units);
  jnum(root, "latitude", &cfg->current.latitude);
  jnum(root, "longitude", &cfg->current.longitude);
  jstr(root, "city", cfg->current.city, sizeof cfg->current.city);
  jstr(root, "country", cfg->current.country, sizeof cfg->current.country);
}

/* config.toml from the Bash versions (flat key = value under [wayweather]) */
static void parse_legacy_toml(const char *text, config_t *cfg) {
  char *copy = xstrdup(text);
  char *save = NULL;
  for (char *line = strtok_r(copy, "\n", &save); line;
       line = strtok_r(NULL, "\n", &save)) {
    char *l = str_trim(line);
    if (*l == '#' || *l == '[' || !*l)
      continue;
    char *eq = strchr(l, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = str_trim(l);
    char *val = str_trim(eq + 1);
    size_t vlen = strlen(val);
    if (vlen >= 2 && val[0] == '"' && val[vlen - 1] == '"') {
      val[vlen - 1] = '\0';
      val++;
    }
    if (strcmp(key, "ip_loc") == 0)
      cfg->geo.enabled = str_ieq(val, "true");
    else if (strcmp(key, "latitude") == 0)
      parse_double(val, &cfg->current.latitude);
    else if (strcmp(key, "longitude") == 0)
      parse_double(val, &cfg->current.longitude);
    else if (strcmp(key, "city") == 0)
      str_copy(cfg->current.city, val, sizeof cfg->current.city);
    else if (strcmp(key, "country") == 0)
      str_copy(cfg->current.country, val, sizeof cfg->current.country);
    else if (strcmp(key, "units") == 0)
      units_from_preset(val, &cfg->current.units);
    else if (strcmp(key, "icons") == 0)
      cfg->display.icons = !str_ieq(val, "false");
    else if (strcmp(key, "use_gps") == 0)
      cfg->geo.provider = str_ieq(val, "true") ? GEO_AUTO : GEO_IP;
  }
  free(copy);
}

/* locations.json: {"0": {...}, "1": {...}} or an array */
static void parse_legacy_locations(const char *text, config_t *cfg) {
  cJSON *root = cJSON_Parse(text);
  if (!root) {
    log_warn("Legacy locations.json is not valid JSON, skipping");
    return;
  }
  const cJSON *it;
  cJSON_ArrayForEach(it, root) {
    location_t loc;
    location_init(&loc);
    parse_location(it, &loc);
    config_add(cfg, &loc);
  }
  cJSON_Delete(root);
}

static bool migrate_legacy(config_t *cfg) {
  const char *dir = xdg_config_dir();
  char *toml_path = xasprintf("%s/config.toml", dir);
  char *locs_path = xasprintf("%s/locations.json", dir);
  bool migrated = false;

  char *text = read_file(toml_path, NULL);
  if (text) {
    parse_legacy_toml(text, cfg);
    free(text);
    log_info("Migrated settings from %s", toml_path);
    migrated = true;
  }
  text = read_file(locs_path, NULL);
  if (text) {
    parse_legacy_locations(text, cfg);
    free(text);
    log_info("Migrated %d saved location(s) from %s", cfg->n_locations,
             locs_path);
    migrated = true;
  }
  if (migrated) {
    cfg->active = config_match_current(cfg);
    /* Only one default may exist */
    int def = config_default_index(cfg);
    config_set_default(cfg, def);
  }
  free(toml_path);
  free(locs_path);
  return migrated;
}

/* ---------- Load / save ---------- */

bool config_load(config_t *cfg) {
  config_defaults(cfg);
  const char *path = config_path();
  char *text = read_file(path, NULL);

  if (!text) {
    if (errno != ENOENT) {
      log_error("Cannot read %s: %s", path, strerror(errno));
      return false;
    }
    if (!custom_path[0] && migrate_legacy(cfg))
      log_info("Legacy files were left untouched; config is now %s", path);
    else
      log_info("Creating default config at %s", path);
    config_save(cfg);
    return true;
  }

  cJSON *root = cJSON_Parse(text);
  if (!cJSON_IsObject(root)) {
    /* The error pointer points into `text`: report the line number */
    const char *where = cJSON_GetErrorPtr();
    int line = 1;
    for (const char *p = text; where && p < where && *p; p++)
      line += *p == '\n';
    if (where)
      log_error("Config %s is not valid JSON (line %d)", path, line);
    else
      log_error("Config %s must contain a JSON object", path);
    cJSON_Delete(root);
    free(text);
    return false;
  }
  free(text);

  if (jget(root, "version")) {
    parse_v3(root, cfg);
  } else {
    log_info("Upgrading config %s to version %d", path, CONFIG_VERSION);
    parse_legacy_json(root, cfg);
    if (!custom_path[0]) {
      char *locs_path = xasprintf("%s/locations.json", xdg_config_dir());
      char *locs = read_file(locs_path, NULL);
      if (locs)
        parse_legacy_locations(locs, cfg);
      free(locs);
      free(locs_path);
    }
    cJSON_Delete(root);
    config_save(cfg);
    return true;
  }
  cJSON_Delete(root);
  return true;
}

bool config_save(const config_t *cfg) {
  char *json = config_to_json(cfg);
  strbuf sb;
  sb_init(&sb);
  sb_append(&sb, json);
  sb_appendc(&sb, '\n');
  cJSON_free(json);
  bool ok = write_file_atomic(config_path(), sb.data, sb.len);
  sb_free(&sb);
  if (ok)
    log_debug("Saved config to %s", config_path());
  return ok;
}

int config_lock(void) {
  /* Lives in the runtime dir so it doesn't clutter ~/.config */
  char *dir = xasprintf("%s/wayweather", xdg_runtime_dir());
  mkdir_p(dir, 0700);
  char *path = xasprintf("%s/config.lock", dir);
  free(dir);
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  free(path);
  if (fd >= 0 && flock(fd, LOCK_EX) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void config_unlock(int fd) {
  if (fd >= 0) {
    flock(fd, LOCK_UN);
    close(fd);
  }
}

/* ---------- Saved locations ---------- */

int config_find(const config_t *cfg, const char *ref) {
  if (str_empty(ref))
    return -1;
  if (str_ieq(ref, "default"))
    return config_default_index(cfg);
  long idx;
  if (parse_long(ref, &idx))
    return idx >= 1 && idx <= cfg->n_locations ? (int)idx - 1 : -1;
  /* Exact name, then exact city, then substring of either */
  for (int i = 0; i < cfg->n_locations; i++)
    if (str_ieq(cfg->locations[i].name, ref))
      return i;
  for (int i = 0; i < cfg->n_locations; i++)
    if (str_ieq(cfg->locations[i].city, ref))
      return i;
  int found = -1;
  for (int i = 0; i < cfg->n_locations; i++) {
    const location_t *l = &cfg->locations[i];
    if (str_icontains(l->name, ref) || str_icontains(l->city, ref)) {
      if (found >= 0)
        return -2; /* ambiguous */
      found = i;
    }
  }
  return found;
}

int config_add(config_t *cfg, const location_t *loc) {
  if (cfg->n_locations >= MAX_LOCATIONS)
    return -1;
  if (cfg->n_locations == cfg->cap_locations) {
    cfg->cap_locations = cfg->cap_locations ? cfg->cap_locations * 2 : 8;
    cfg->locations = xrealloc(cfg->locations,
                              sizeof *cfg->locations * cfg->cap_locations);
  }
  int idx = cfg->n_locations++;
  cfg->locations[idx] = *loc;
  if (loc->is_default)
    config_set_default(cfg, idx);
  return idx;
}

void config_remove(config_t *cfg, int idx) {
  if (idx < 0 || idx >= cfg->n_locations)
    return;
  memmove(&cfg->locations[idx], &cfg->locations[idx + 1],
          sizeof *cfg->locations * (cfg->n_locations - idx - 1));
  cfg->n_locations--;
  if (cfg->active == idx)
    cfg->active = -1;
  else if (cfg->active > idx)
    cfg->active--;
}

void config_move(config_t *cfg, int from, int to) {
  if (from < 0 || from >= cfg->n_locations || to < 0 ||
      to >= cfg->n_locations || from == to)
    return;
  location_t tmp = cfg->locations[from];
  if (from < to)
    memmove(&cfg->locations[from], &cfg->locations[from + 1],
            sizeof tmp * (to - from));
  else
    memmove(&cfg->locations[to + 1], &cfg->locations[to],
            sizeof tmp * (from - to));
  cfg->locations[to] = tmp;

  if (cfg->active == from)
    cfg->active = to;
  else if (from < to && cfg->active > from && cfg->active <= to)
    cfg->active--;
  else if (from > to && cfg->active >= to && cfg->active < from)
    cfg->active++;
}

void config_set_default(config_t *cfg, int idx) {
  for (int i = 0; i < cfg->n_locations; i++)
    cfg->locations[i].is_default = (i == idx);
}

int config_default_index(const config_t *cfg) {
  for (int i = 0; i < cfg->n_locations; i++)
    if (cfg->locations[i].is_default)
      return i;
  return -1;
}

void config_use(config_t *cfg, int idx) {
  if (idx < 0 || idx >= cfg->n_locations)
    return;
  cfg->current = cfg->locations[idx];
  cfg->current.is_default = false;
  cfg->active = idx;
  cfg->geo.enabled = false;
}

bool location_same_place(const location_t *a, const location_t *b) {
  return fabs(a->latitude - b->latitude) < 0.01 &&
         fabs(a->longitude - b->longitude) < 0.01;
}

int config_match_current(const config_t *cfg) {
  if (cfg->active >= 0 && cfg->active < cfg->n_locations &&
      location_same_place(&cfg->locations[cfg->active], &cfg->current))
    return cfg->active;
  for (int i = 0; i < cfg->n_locations; i++)
    if (location_same_place(&cfg->locations[i], &cfg->current))
      return i;
  return -1;
}

void location_label(const location_t *loc, char *buf, size_t size) {
  if (!str_empty(loc->name))
    str_copy(buf, loc->name, size);
  else if (!str_empty(loc->city))
    str_copy(buf, loc->city, size);
  else
    snprintf(buf, size, "%.4f, %.4f", loc->latitude, loc->longitude);
}

void location_place(const location_t *loc, char *buf, size_t size) {
  const char *parts[] = {loc->city, loc->region, loc->country};
  strbuf sb;
  sb_init(&sb);
  for (size_t i = 0; i < ARRAY_LEN(parts); i++) {
    if (str_empty(parts[i]))
      continue;
    /* Skip a region identical to the city (e.g. "Vienna, Vienna") */
    if (i == 1 && str_ieq(parts[1], parts[0]))
      continue;
    if (sb.len)
      sb_append(&sb, ", ");
    sb_append(&sb, parts[i]);
  }
  if (!sb.len)
    sb_printf(&sb, "%.4f, %.4f", loc->latitude, loc->longitude);
  str_copy(buf, sb.data, size);
  sb_free(&sb);
}

bool location_validate(const location_t *loc, char *err, size_t errlen) {
  if (!isfinite(loc->latitude) || loc->latitude < -90 || loc->latitude > 90) {
    snprintf(err, errlen, "Latitude must be between -90 and 90");
    return false;
  }
  if (!isfinite(loc->longitude) || loc->longitude < -180 ||
      loc->longitude > 180) {
    snprintf(err, errlen, "Longitude must be between -180 and 180");
    return false;
  }
  if (str_empty(loc->name) && str_empty(loc->city)) {
    snprintf(err, errlen, "Give the location a name or a city");
    return false;
  }
  return true;
}
