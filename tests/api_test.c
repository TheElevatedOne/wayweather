/* Offline checks of the public libwayweather API (no network access) */

#include <wayweather.h>

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (cond) {                                                                \
      printf("  ok    %s\n", #cond);                                           \
    } else {                                                                   \
      printf("  FAIL  %s (%s:%d)\n", #cond, __FILE__, __LINE__);               \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void sample(ww_location *loc, ww_weather *w) {
  memset(loc, 0, sizeof *loc);
  snprintf(loc->name, sizeof loc->name, "Home");
  snprintf(loc->city, sizeof loc->city, "Vienna");
  snprintf(loc->country, sizeof loc->country, "Austria");
  loc->latitude = 48.2085;
  loc->longitude = 16.3721;
  ww_units_metric(&loc->units);

  memset(w, 0, sizeof *w);
  snprintf(w->time, sizeof w->time, "2026-10-17T18:15");
  w->temperature = 13.26;
  w->apparent_temperature = 10.6;
  w->humidity = 49;
  w->dew_point = NAN;
  w->pressure_hpa = 1016;
  w->wind_speed = 6.6;
  w->wind_direction = 315;
  w->code = 3;
  w->is_day = 0;
  w->units = loc->units;
}

int main(void) {
  /* A comma decimal separator must not leak into the library's output */
  if (!setlocale(LC_ALL, "de_DE.UTF-8"))
    setlocale(LC_ALL, "C.UTF-8");

  printf("libwayweather %s API tests\n", ww_version());
  CHECK(ww_init() == 0);
  CHECK(strlen(ww_version()) > 0);

  ww_units u;
  ww_units_imperial(&u);
  CHECK(u.temperature == WW_TEMP_FAHRENHEIT && u.clock == WW_CLOCK_12H);
  CHECK(strcmp(ww_temp_label(u.temperature), "°F") == 0);
  CHECK(strcmp(ww_wind_label(WW_WIND_KN), "kn") == 0);
  CHECK(strcmp(ww_pressure_label((ww_pressure_unit)42), "") == 0);
  CHECK(fabs(ww_pressure_from_hpa(1000, WW_PRESSURE_KPA) - 100) < 1e-9);

  CHECK(strcmp(ww_wmo_description(61), "Light rain") == 0);
  CHECK(strcmp(ww_wmo_class(95), "thunder") == 0);
  CHECK(strcmp(ww_wmo_description(1234), "Unknown") == 0);
  CHECK(strcmp(ww_wind_compass(315), "NW") == 0);
  CHECK(strcmp(ww_wind_compass(359), "N") == 0);

  ww_location loc;
  ww_weather w;
  sample(&loc, &w);
  char *s = ww_format("{name}: {temp}{tunit} {desc}, {wind_dir} [{dew}]", &loc,
                      &w, 1, 0);
  CHECK(s && strcmp(s, "Home: 13.3°C Overcast, NW [--]") == 0);
  if (s)
    printf("        -> \"%s\"\n", s);
  free(s);

  s = ww_format("{lat},{lon} {{literal}} {unknown}", &loc, &w, 0, 0);
  CHECK(s && strcmp(s, "48.2085,16.3721 {literal} {unknown}") == 0);
  free(s);

  CHECK(ww_format(NULL, &loc, &w, 1, 1) == NULL);

  char err[128] = "";
  CHECK(ww_geocode("x", &loc, 1, err, sizeof err) == -1 && err[0]);

  ww_cleanup();
  printf("\n%s\n", failures ? "API tests FAILED" : "API tests passed");
  return failures ? 1 : 0;
}
