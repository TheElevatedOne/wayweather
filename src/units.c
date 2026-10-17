#include "units.h"
#include "util.h"

#include <stdio.h>

const char *const TEMP_IDS[TEMP_COUNT] = {"celsius", "fahrenheit"};
const char *const WIND_IDS[WIND_COUNT] = {"kmh", "ms", "mph", "kn"};
const char *const PRECIP_IDS[PRECIP_COUNT] = {"mm", "inch"};
const char *const PRESSURE_IDS[PRESSURE_COUNT] = {"hpa", "inhg", "mmhg",
                                                  "kpa"};
const char *const CLOCK_IDS[CLOCK_COUNT] = {"24h", "12h"};

const char *const TEMP_LABELS[TEMP_COUNT] = {"°C", "°F"};
const char *const WIND_LABELS[WIND_COUNT] = {"km/h", "m/s", "mph", "kn"};
const char *const PRECIP_LABELS[PRECIP_COUNT] = {"mm", "in"};
const char *const PRESSURE_LABELS[PRESSURE_COUNT] = {"hPa", "inHg", "mmHg",
                                                     "kPa"};
const char *const CLOCK_LABELS[CLOCK_COUNT] = {"24h", "12h"};

int unit_lookup(const char *const *ids, int count, const char *id) {
  for (int i = 0; i < count; i++)
    if (str_ieq(ids[i], id))
      return i;
  return -1;
}

void units_metric(units_t *u) {
  *u = (units_t){TEMP_CELSIUS, WIND_KMH, PRECIP_MM, PRESSURE_HPA, CLOCK_24H};
}

void units_imperial(units_t *u) {
  *u = (units_t){TEMP_FAHRENHEIT, WIND_MPH, PRECIP_INCH, PRESSURE_INHG,
                 CLOCK_12H};
}

bool units_from_preset(const char *name, units_t *u) {
  if (str_ieq(name, "metric") || str_ieq(name, "met")) {
    units_metric(u);
    return true;
  }
  if (str_ieq(name, "imperial") || str_ieq(name, "imp")) {
    units_imperial(u);
    return true;
  }
  return false;
}

static bool units_eq(const units_t *a, const units_t *b) {
  return a->temperature == b->temperature && a->wind == b->wind &&
         a->precipitation == b->precipitation && a->pressure == b->pressure &&
         a->clock == b->clock;
}

const char *units_preset_name(const units_t *u) {
  units_t p;
  units_metric(&p);
  if (units_eq(u, &p))
    return "metric";
  units_imperial(&p);
  if (units_eq(u, &p))
    return "imperial";
  return "custom";
}

void units_describe(const units_t *u, char *buf, size_t size) {
  snprintf(buf, size, "%s, %s, %s, %s, %s", TEMP_LABELS[u->temperature],
           WIND_LABELS[u->wind], PRECIP_LABELS[u->precipitation],
           PRESSURE_LABELS[u->pressure], CLOCK_LABELS[u->clock]);
}

double pressure_from_hpa(double hpa, pressure_unit unit) {
  switch (unit) {
  case PRESSURE_INHG:
    return hpa * 0.0295299830714;
  case PRESSURE_MMHG:
    return hpa * 0.750061683;
  case PRESSURE_KPA:
    return hpa / 10.0;
  default:
    return hpa;
  }
}

double temp_to_celsius(double value, temp_unit unit) {
  return unit == TEMP_FAHRENHEIT ? (value - 32.0) * 5.0 / 9.0 : value;
}
