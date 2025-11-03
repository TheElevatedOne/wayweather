#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "api-parser.h"
#include "config.h"
#include "logging.h"

void waybar(struct data *meteo_data, struct config *config_data, bool no_icon) {
  char *icon = meteo_data->icon;
  if (no_icon) {
    icon = "";
  }

  char json[512];
  sprintf(json,
          "{\"text\":\"%.1f%s%s\",\"tooltip\":\"%s, %s\\n%s "
          "%s\\n\\nTemperature: %.1f%s (%.1f%s)\\nHumidity: "
          "%d%s\\nPrecipitation: %.1f %s\\nCloud Cover: %d%s\\nWind Speed: "
          "%.1f %s\\nWind Gusts: %.1f %s\\nWind Direction: %s "
          "(%d%s)\\nPressure: %.1f %s}",
          meteo_data->temperature, meteo_data->temperature_unit, icon,
          config_data->city, config_data->country, meteo_data->time,
          meteo_data->date, meteo_data->temperature,
          meteo_data->temperature_unit, meteo_data->apparent_temperature,
          meteo_data->temperature_unit, meteo_data->humidity,
          meteo_data->humidity_unit, meteo_data->precipitation,
          meteo_data->precipitation_unit, meteo_data->cloud_cover,
          meteo_data->cloud_cover_unit, meteo_data->wind_speed,
          meteo_data->wind_speed_unit, meteo_data->wind_gusts,
          meteo_data->wind_speed_unit, meteo_data->wind_direction_string,
          meteo_data->wind_direction, meteo_data->wind_direction_unit,
          meteo_data->pressure, meteo_data->pressure_unit);
  printf("%s\n", json);
}
