#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/typecheck-gcc.h>

#include "logging.h"

struct memory {
  char *data;
  size_t size;
};

struct data {
  char *icon; // Nerd Icon
  char *time; // Weather Update Time
  char *date; // Weather Update Date
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

static size_t ResponseCallback(void *contents, size_t size, size_t nmemb,
                               void *userp) {
  size_t totalSize = size * nmemb;
  struct memory *mem = (struct memory *)userp;

  char *ptr = realloc(mem->data, mem->size + totalSize + 1);
  if (ptr == NULL) {
    fprintf(stderr, "Not enough memory to allocate buffer.\n");
    return 0;
  }

  mem->data = ptr;
  memcpy(&(mem->data[mem->size]), contents, totalSize);
  mem->size += totalSize;
  mem->data[mem->size] = '\0';

  return totalSize;
}

static char *getResponse(const float latitude, const float longitude,
                         const bool units, const bool logs) {
  clock_t start = clock();
  // When units are true, it assumes metric
  // When units are false, it assumes imperial
  char *temp_units = "";
  char *wind_units = "";
  char *prec_units = "";

  if (!units) {
    temp_units = "&temperature_unit=fahrenheit";
    wind_units = "&wind_speed_unit=mph";
    prec_units = "&precipitation_unit=inch";
  }

  char url[512];
  sprintf(
      url,
      "https://api.open-meteo.com/v1/"
      "forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,pressure_"
      "msl,apparent_temperature,relative_humidity_2m,precipitation,cloud_cover,"
      "wind_speed_10m,wind_direction_10m,wind_gusts_10m,is_day,weather_code&"
      "timezone=auto%s%s%s",
      latitude, longitude, temp_units, wind_units, prec_units);

  CURL *curl;
  CURLcode res;
  struct memory response;
  char *json = malloc(1024);

  response.data = malloc(1024);
  response.size = 0;

  curl_global_init(CURL_GLOBAL_DEFAULT);
  curl = curl_easy_init();

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ResponseCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

    res = curl_easy_perform(curl);

    json = response.data;

    curl_easy_cleanup(curl);
  } else {
    logger(1, "Meteo API Fetch Failed");
    exit(1);
  }

  curl_global_cleanup();

  clock_t end = clock();
  float runtime = (float)(end - start) / 1000;

  if (logs) {
    char *message = malloc(128);
    sprintf(message, "API Fetch took %.1f ms", runtime);
    logger(3, message);
    free(message);
  }

  return json;
}

static char *iconParser(const int is_day, const int weather_code) {
  char *icon = malloc(16);

  if (weather_code <= 2) {
    if (is_day == 0 && weather_code == 0) {
      icon = " 󰖔 ";
    } else if (is_day == 0 && weather_code == 1) {
      icon = " 󰖔 ";
    } else if (is_day == 0 && weather_code == 2) {
      icon = " 󰼱 ";
    } else if (is_day == 1 && weather_code == 0) {
      icon = " 󰖙 ";
    } else if (is_day == 1 && weather_code == 1) {
      icon = " 󰖙 ";
    } else {
      icon = " 󰖕 ";
    }
  } else {
    switch (weather_code) {
    case 3:
      icon = " 󰖐 ";
    case 45:
      icon = " 󰖑 ";
    case 48:
      icon = " 󰖑 ";
    case 51:
      icon = " 󰖗 ";
    case 53:
      icon = " 󰖗 ";
    case 55:
      icon = " 󰖗 ";
    case 56:
      icon = " 󰙿 ";
    case 57:
      icon = " 󰙿 ";
    case 61:
      icon = " 󰖖 ";
    case 63:
      icon = " 󰖖 ";
    case 65:
      icon = " 󰖖 ";
    case 66:
      icon = " 󰙿 ";
    case 67:
      icon = " 󰙿 ";
    case 71:
      icon = " 󰖘 ";
    case 73:
      icon = " 󰖘 ";
    case 75:
      icon = " 󰼶 ";
    case 77:
      icon = " 󰖒 ";
    case 80:
      icon = " 󰖖 ";
    case 81:
      icon = " 󰖖 ";
    case 82:
      icon = " 󰖖 ";
    case 85:
      icon = " 󰼶 ";
    case 86:
      icon = " 󰼶 ";
    case 95:
      icon = " 󰖓 ";
    case 96:
      icon = " 󰖓 ";
    case 99:
      icon = " 󰖓 ";
    }
  }

  return icon;
}

struct data *getMeteoData(const float latitude, const float longitude,
                          const bool unit, const bool logs) {

  // Get API response and parse it as JSON
  char *r_json = malloc(1024);
  r_json = getResponse(latitude, longitude, unit, logs);

  clock_t start = clock();

  cJSON *json = cJSON_Parse(r_json);

  // Use struct and allocate chars
  struct data *meteo_data = malloc(sizeof(struct data));
  // meteo_data->icon = malloc(16);
  // meteo_data->time = malloc(16);
  // meteo_data->date = malloc(16);
  // meteo_data->wind_direction_string = malloc(2);
  // meteo_data->temperature_unit = malloc(2);
  // meteo_data->humidity_unit = malloc(1);
  // meteo_data->precipitation_unit = malloc(4);
  // meteo_data->cloud_cover_unit = malloc(1);
  // meteo_data->wind_speed_unit = malloc(4);
  // meteo_data->wind_direction_unit = malloc(1);
  // meteo_data->pressure_unit = malloc(4);

  // Get Data and Units JSON objects
  cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "current");
  cJSON *units = cJSON_GetObjectItemCaseSensitive(json, "current_units");

  // Time Parsing
  char *datetime = cJSON_GetObjectItemCaseSensitive(data, "time")->valuestring;
  meteo_data->date = strtok(datetime, "T");
  meteo_data->time = strtok(NULL, "T");

  // Icon Parsing
  int is_day = cJSON_GetObjectItemCaseSensitive(data, "is_day")->valueint;
  int weather_code =
      cJSON_GetObjectItemCaseSensitive(data, "weather_code")->valueint;
  char *icon = iconParser(is_day, weather_code);
  meteo_data->icon = icon;

  // Assign chars
  meteo_data->temperature_unit =
      cJSON_GetObjectItemCaseSensitive(units, "temperature_2m")->valuestring;
  meteo_data->humidity_unit =
      cJSON_GetObjectItemCaseSensitive(units, "relative_humidity_2m")
          ->valuestring;
  meteo_data->precipitation_unit =
      cJSON_GetObjectItemCaseSensitive(units, "precipitation")->valuestring;
  meteo_data->cloud_cover_unit =
      cJSON_GetObjectItemCaseSensitive(units, "cloud_cover")->valuestring;
  meteo_data->wind_speed_unit =
      cJSON_GetObjectItemCaseSensitive(units, "wind_speed_10m")->valuestring;
  meteo_data->wind_direction_unit =
      cJSON_GetObjectItemCaseSensitive(units, "wind_direction_10m")
          ->valuestring;
  meteo_data->pressure_unit =
      cJSON_GetObjectItemCaseSensitive(units, "pressure_msl")->valuestring;

  // Assign ints
  meteo_data->humidity =
      cJSON_GetObjectItemCaseSensitive(data, "relative_humidity_2m")->valueint;
  meteo_data->cloud_cover =
      cJSON_GetObjectItemCaseSensitive(data, "cloud_cover")->valueint;
  meteo_data->wind_direction =
      cJSON_GetObjectItemCaseSensitive(data, "wind_direction_10m")->valueint;

  // Wind Direction Parsing
  int wind_dir = (meteo_data->wind_direction / 45) % 8;
  switch (wind_dir) {
  case 0:
    meteo_data->wind_direction_string = "N";
  case 1:
    meteo_data->wind_direction_string = "NE";
  case 2:
    meteo_data->wind_direction_string = "E";
  case 3:
    meteo_data->wind_direction_string = "SE";
  case 4:
    meteo_data->wind_direction_string = "S";
  case 5:
    meteo_data->wind_direction_string = "SW";
  case 6:
    meteo_data->wind_direction_string = "W";
  case 7:
    meteo_data->wind_direction_string = "NW";
  }

  // Assign floats
  meteo_data->temperature =
      cJSON_GetObjectItemCaseSensitive(data, "temperature_2m")->valuedouble;
  meteo_data->apparent_temperature =
      cJSON_GetObjectItemCaseSensitive(data, "apparent_temperature")
          ->valuedouble;
  meteo_data->precipitation =
      cJSON_GetObjectItemCaseSensitive(data, "precipitation")->valuedouble;
  meteo_data->wind_speed =
      cJSON_GetObjectItemCaseSensitive(data, "wind_speed_10m")->valuedouble;
  meteo_data->wind_gusts =
      cJSON_GetObjectItemCaseSensitive(data, "wind_gusts_10m")->valuedouble;
  meteo_data->pressure =
      cJSON_GetObjectItemCaseSensitive(data, "pressure_msl")->valuedouble;

  clock_t end = clock();
  float runtime = (float)(end - start) / 1000;

  if (logs) {
    char *message = malloc(128);
    sprintf(message, "API Parsing took %.1f ms", runtime);
    logger(3, message);
    free(message);
  }

  return meteo_data;
}
