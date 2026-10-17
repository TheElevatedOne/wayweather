#ifndef WW_UNITS_H_
#define WW_UNITS_H_

#include <stdbool.h>
#include <stddef.h>

typedef enum { TEMP_CELSIUS, TEMP_FAHRENHEIT, TEMP_COUNT } temp_unit;
typedef enum { WIND_KMH, WIND_MS, WIND_MPH, WIND_KN, WIND_COUNT } wind_unit;
typedef enum { PRECIP_MM, PRECIP_INCH, PRECIP_COUNT } precip_unit;
typedef enum {
  PRESSURE_HPA,
  PRESSURE_INHG,
  PRESSURE_MMHG,
  PRESSURE_KPA,
  PRESSURE_COUNT
} pressure_unit;
typedef enum { CLOCK_24H, CLOCK_12H, CLOCK_COUNT } clock_format;

/* The "measurements" each location is displayed in */
typedef struct {
  temp_unit temperature;
  wind_unit wind;
  precip_unit precipitation;
  pressure_unit pressure;
  clock_format clock;
} units_t;

/* Identifiers used in the config file and the Open-Meteo API */
extern const char *const TEMP_IDS[TEMP_COUNT];
extern const char *const WIND_IDS[WIND_COUNT];
extern const char *const PRECIP_IDS[PRECIP_COUNT];
extern const char *const PRESSURE_IDS[PRESSURE_COUNT];
extern const char *const CLOCK_IDS[CLOCK_COUNT];

/* Human readable unit symbols */
extern const char *const TEMP_LABELS[TEMP_COUNT];
extern const char *const WIND_LABELS[WIND_COUNT];
extern const char *const PRECIP_LABELS[PRECIP_COUNT];
extern const char *const PRESSURE_LABELS[PRESSURE_COUNT];
extern const char *const CLOCK_LABELS[CLOCK_COUNT];

/* Index of `id` in `ids` (case-insensitive), or -1 */
int unit_lookup(const char *const *ids, int count, const char *id);

void units_metric(units_t *u);
void units_imperial(units_t *u);
/* Accepts "metric"/"met"/"imperial"/"imp" */
bool units_from_preset(const char *name, units_t *u);
/* "metric", "imperial" or "custom" */
const char *units_preset_name(const units_t *u);
/* e.g. "°C, km/h, mm, hPa, 24h" */
void units_describe(const units_t *u, char *buf, size_t size);

double pressure_from_hpa(double hpa, pressure_unit unit);
double temp_to_celsius(double value, temp_unit unit);

#endif // !WW_UNITS_H_
