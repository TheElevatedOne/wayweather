#ifndef WW_CONFIG_H_
#define WW_CONFIG_H_

#include "units.h"
#include <stdbool.h>
#include <stddef.h>

#define CONFIG_VERSION 3
#define FIELD_LEN 96
#define FORMAT_LEN 256
#define MAX_LOCATIONS 256

typedef struct {
  char name[FIELD_LEN]; /* optional label, e.g. "Home" */
  char city[FIELD_LEN];
  char region[FIELD_LEN];
  char country[FIELD_LEN];
  double latitude;
  double longitude;
  units_t units;
  bool is_default;
} location_t;

typedef enum {
  GEO_AUTO, /* gpsd -> GeoClue -> IP */
  GEO_GPSD,
  GEO_GEOCLUE,
  GEO_IP,
  GEO_PROVIDER_COUNT
} geo_provider;

extern const char *const GEO_PROVIDER_IDS[GEO_PROVIDER_COUNT];

typedef struct {
  bool enabled;          /* overrides the current location when true */
  geo_provider provider;
  char gpsd_host[FIELD_LEN];
  int gpsd_port;
  int timeout;           /* seconds per provider attempt */
  int cache_ttl;         /* seconds a geolocation fix is reused */
  bool reverse_geocode;  /* look up city names for GPS fixes */
  bool in_cycle;         /* `next`/`prev` also step through geolocation */
} geo_config;

typedef struct {
  bool icons;              /* Nerd Font weather glyphs */
  char text[FORMAT_LEN];   /* Waybar bar text template */
  char tooltip_title[FORMAT_LEN];
  bool tooltip;
  int hours;               /* hourly entries in tooltip / print */
  int days;                /* daily entries in tooltip / print */
  int decimals;            /* decimals for temperatures */
} display_config;

typedef struct {
  location_t current; /* location used when geolocation is off */
  int active;         /* index of saved location loaded into current or -1 */
  geo_config geo;
  display_config display;
  int refresh_interval; /* daemon refresh, seconds */
  int cache_ttl;        /* weather cache lifetime, seconds */
  location_t *locations;
  int n_locations;
  int cap_locations;
} config_t;

/* Path handling (an explicit path may be set with -c/--config) */
void config_set_path(const char *path);
const char *config_path(void);

void config_defaults(config_t *cfg);
/* Loads the config, creating it (or migrating a legacy one) if missing.
 * Returns false only if the file exists but cannot be parsed. */
bool config_load(config_t *cfg);
bool config_save(const config_t *cfg);
void config_free(config_t *cfg);
/* Serialises the config into a pretty-printed JSON string (caller frees) */
char *config_to_json(const config_t *cfg);
/* Saved locations as a JSON array, with 1-based "id" (caller frees) */
char *config_locations_json(const config_t *cfg);

/* Exclusive advisory lock for read-modify-write cycles; returns fd or -1 */
int config_lock(void);
void config_unlock(int fd);

/* Saved locations */
void location_init(location_t *loc);
/* Resolves a 1-based index, "default", "current"-less name or city match */
int config_find(const config_t *cfg, const char *ref);
int config_add(config_t *cfg, const location_t *loc);
void config_remove(config_t *cfg, int idx);
void config_move(config_t *cfg, int from, int to);
void config_set_default(config_t *cfg, int idx); /* -1 clears */
int config_default_index(const config_t *cfg);
/* Copies a saved location into current and disables geolocation */
void config_use(config_t *cfg, int idx);
/* Index of the saved location matching current coordinates, or -1 */
int config_match_current(const config_t *cfg);

/* "Name" or "City" or "lat, lon" */
void location_label(const location_t *loc, char *buf, size_t size);
/* "City, Region, Country" without empty parts */
void location_place(const location_t *loc, char *buf, size_t size);
bool location_validate(const location_t *loc, char *err, size_t errlen);
bool location_same_place(const location_t *a, const location_t *b);

#endif // !WW_CONFIG_H_
