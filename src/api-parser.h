#ifndef API_PARSER_H_
#define API_PARSER_H_

#include <stdbool.h>
#include <stdlib.h>

struct memory {
  char *data;
  size_t size;
};

/* Struct for Meteo Data */
struct data {
  char *icon;
  char *time;
  char *date;
  char *temperature_unit;
  char *humidity_unit;
  char *precipitation_unit;
  char *cloud_cover_unit;
  char *wind_direction_string;
  char *wind_speed_unit;
  char *wind_direction_unit;
  char *pressure_unit;

  int humidity;
  int cloud_cover;
  int wind_direction;

  float temperature;
  float apparent_temperature;
  float precipitation;
  float wind_speed;
  float wind_gusts;
  float pressure;
};

/* Parses API Fetch and Returns Meteo Struct */
struct data *getMeteoData(const float latitude, const float longitude,
                          const bool unit, const bool logs);

#endif // !API_PARSER_H_
