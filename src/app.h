#ifndef WW_APP_H_
#define WW_APP_H_

#include "config.h"
#include "weather.h"
#include <stdbool.h>
#include <stddef.h>

/* Decides which location to show:
 *   `ref` set       -> that saved location (1-based index, name, "default")
 *   geolocation on  -> device position, units of the current location
 *   otherwise       -> the current location
 * `source` receives a short description such as "saved", "config", "ip". */
bool app_resolve(const config_t *cfg, const char *ref, bool force,
                 location_t *out, char *source, size_t source_len, char *err,
                 size_t errlen);

/* Resolves the location and fetches its weather in one go */
bool app_fetch(const config_t *cfg, const char *ref, bool force, int ttl,
               location_t *loc, weather_t *w, char *source, size_t source_len,
               char *err, size_t errlen);

/* Steps the current location through the saved ones (and geolocation if
 * geo.in_cycle is set). Returns a description of the new selection. */
bool app_cycle(config_t *cfg, int direction, char *desc, size_t desc_len);

/* Whether `loc` was ever set to something meaningful */
bool app_location_set(const location_t *loc);

#endif // !WW_APP_H_
