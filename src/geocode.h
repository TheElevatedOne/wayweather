#ifndef WW_GEOCODE_H_
#define WW_GEOCODE_H_

#include "config.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  location_t loc; /* city, region, country, latitude, longitude */
  char country_code[4];
  long population;
  double elevation;
  char timezone[64];
} geocode_result;

/* Searches places by name via the Open-Meteo geocoding API.
 * "City, Region/Country" narrows results by the part after the comma.
 * Returns the number of results (0..max) or -1 on error. */
int geocode_search(const char *query, int max, geocode_result *out,
                   char *err, size_t errlen);

/* Fills city/region/country for coordinates (OpenStreetMap Nominatim),
 * cached on disk. Returns false if nothing could be resolved. */
bool geocode_reverse(double lat, double lon, location_t *loc);

#endif // !WW_GEOCODE_H_
