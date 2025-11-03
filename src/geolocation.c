#include "api-parser.h"
#include "config.h"
#include "logging.h"
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

struct config *locateIP(const bool logs) {
  clock_t start = clock();

  struct config *config_data = malloc(sizeof(struct config));

  char url[22] = "http://ip-api.com/json";
  CURL *curl;
  CURLcode res;
  struct memory response;
  char *json = malloc(512);

  response.data = malloc(0);
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
    logger(1, "Geolocation API Fetch Failed");
    exit(1);
  }

  curl_global_cleanup();

  cJSON *json_ = cJSON_Parse(json);

  config_data->geolocation = true;
  config_data->latitude =
      cJSON_GetObjectItemCaseSensitive(json_, "lat")->valuedouble;
  config_data->longitude =
      cJSON_GetObjectItemCaseSensitive(json_, "lon")->valuedouble;
  config_data->city =
      cJSON_GetObjectItemCaseSensitive(json_, "city")->valuestring;
  config_data->country =
      cJSON_GetObjectItemCaseSensitive(json_, "country")->valuestring;

  clock_t end = clock();
  float runtime = (float)(end - start) / 1000;

  if (logs) {
    char *message = malloc(128);
    sprintf(message, "API Fetch took %.1f ms", runtime);
    logger(3, message);
    free(message);
  }
  return config_data;
}
