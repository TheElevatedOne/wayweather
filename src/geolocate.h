#ifndef WW_GEOLOCATE_H_
#define WW_GEOLOCATE_H_

#include "config.h"
#include <stdbool.h>
#include <stddef.h>

/* Determines the device position using the configured provider chain.
 * Fills latitude/longitude/city/region/country of `out` (units untouched)
 * and names the provider that answered in `source`.
 * Fixes are cached for geo->cache_ttl seconds unless `force` is set. */
bool geolocate(const geo_config *geo, bool force, location_t *out,
               char *source, size_t source_len, char *err, size_t errlen);

/* Whether GeoClue support was compiled in */
bool geolocate_has_geoclue(void);

#endif // !WW_GEOLOCATE_H_
