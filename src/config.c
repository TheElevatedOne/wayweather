#include "logging.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <time.h>

/* Values for config file */
struct config {
  bool geolocation;
  bool units;
  float latitude;
  float longitude;
  char *city;
  char *country;
};

/* Check if config directory exists */
static bool isDir(const char *directory) {
  DIR *dir = opendir(directory);
  if (dir) {
    closedir(dir);
    return true;
  } else if (ENOENT == errno) {
    return false;
  } else {
    logger(1, "Config Directory is inaccessible");
    exit(1);
  }
  return false;
}

/* Types: 0 -> Config, 1 -> Locations */
static char *getConfigPath(const int type) {
  char *home_dir = getenv("HOME");
  char *config_dir[3] = {home_dir, ".config", "wayweather"};
  char *full_path = malloc(64);
  for (int i = 0; i < 3; i++) {
    switch (i) {
    case 0:
      if (!isDir(config_dir[0])) {
        logger(1, "Config: Home Directory not found");
        free(full_path);
        exit(1);
      }
    case 1:
      sprintf(full_path, "%s/%s", config_dir[0], config_dir[1]);
      if (!isDir(full_path)) {
        int err = mkdir(full_path, 0700);
        if (err == -1) {
          logger(1, "Failed to create a config directory");
          free(full_path);
          exit(1);
        }
      }
    case 2:
      sprintf(full_path, "%s/%s/%s", config_dir[0], config_dir[1],
              config_dir[2]);
      if (!isDir(full_path)) {
        int err = mkdir(full_path, 0700);
        if (err == -1) {
          logger(1, "Failed to create a config directory");
          free(full_path);
          exit(1);
        }
      }
    }
  }
  sprintf(full_path, "%s/%s/%s/config.json", config_dir[0], config_dir[1],
          config_dir[2]);
  return full_path;
}

/* Create cJSON Object and Output JSON String */
static char *createConfigObject(const struct config *config_data) {
  cJSON *config_json = cJSON_CreateObject();
  cJSON_AddItemToObject(config_json, "geolocation",
                        cJSON_CreateBool(config_data->geolocation));
  cJSON_AddItemToObject(config_json, "units",
                        cJSON_CreateBool(config_data->units));
  cJSON_AddItemToObject(config_json, "latitude",
                        cJSON_CreateNumber(config_data->latitude));
  cJSON_AddItemToObject(config_json, "longitude",
                        cJSON_CreateNumber(config_data->longitude));
  cJSON_AddItemToObject(config_json, "city",
                        cJSON_CreateString(config_data->city));
  cJSON_AddItemToObject(config_json, "country",
                        cJSON_CreateString(config_data->country));
  char *string_json = cJSON_Print(config_json);
  cJSON_Delete(config_json);
  return string_json;
}

/* Parse JSON string into Config Struct */
static struct config *readConfigObject(char *config_json) {
  struct config *config_data = malloc(sizeof(struct config));

  cJSON *config = cJSON_Parse(config_json);
  if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(config, "geolocation")) !=
      0) {
    config_data->geolocation = true;
  } else {
    config_data->geolocation = false;
  }
  if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(config, "units")) != 0) {
    config_data->units = true;
  } else {
    config_data->units = false;
  }

  config_data->latitude =
      cJSON_GetObjectItemCaseSensitive(config, "latitude")->valuedouble;
  config_data->longitude =
      cJSON_GetObjectItemCaseSensitive(config, "longitude")->valuedouble;
  config_data->city =
      cJSON_GetObjectItemCaseSensitive(config, "city")->valuestring;
  config_data->country =
      cJSON_GetObjectItemCaseSensitive(config, "country")->valuestring;

  return config_data;
}

static void createConfig() {
  struct config *config_data = malloc(sizeof(struct config));

  config_data->geolocation = true;
  config_data->units = true;
  config_data->latitude = 0.0;
  config_data->longitude = 0.0;
  config_data->city = "";
  config_data->country = "";

  char *config = malloc(512);
  config = createConfigObject(config_data);
  free(config_data);
  char *full_path = getConfigPath(0);
  FILE *file = fopen(full_path, "w");
  fprintf(file, "%s", config);
  fclose(file);
  free(full_path);
  free(config_data);
}

void resetConfig(const bool logs, const char *units) {
  clock_t start = clock();

  struct config *config_data = malloc(sizeof(struct config));

  config_data->geolocation = true;
  if (strcmp(units, "metric")) {
    config_data->units = true;
  } else if (strcmp(units, "imperial")) {
    config_data->units = false;
  }
  config_data->latitude = 0.0;
  config_data->longitude = 0.0;
  config_data->city = "";
  config_data->country = "";

  char *config = malloc(512);
  config = createConfigObject(config_data);
  free(config_data);
  char *full_path = getConfigPath(0);

  struct stat buffer;
  int exists = stat(full_path, &buffer);
  if (exists != 0) {
    createConfig();
  }

  FILE *file = fopen(full_path, "w");
  fprintf(file, "%s", config);
  fclose(file);
  free(full_path);

  clock_t end = clock();
  float runtime = (float)(end - start) / 1000;

  if (logs) {
    char *message = malloc(128);
    sprintf(message, "Config Reset took %.1f ms", runtime);
    logger(3, message);
    free(message);
  }
}

struct config *readConfig(bool logs) {
  clock_t start = clock();

  char *full_path = getConfigPath(0);

  struct stat buffer;
  int exists = stat(full_path, &buffer);
  if (exists != 0) {
    createConfig();
  }

  char *config = 0;
  FILE *file = fopen(full_path, "r");
  free(full_path);
  if (file) {
    fseek(file, 0, SEEK_END);
    ulong length = ftell(file);
    fseek(file, 0, SEEK_SET);
    config = malloc(length);
    if (config) {
      fread(config, 1, length, file);
    }
    fclose(file);
  } else {
    logger(1, "Failed to open Config File");
    exit(1);
  }

  struct config *config_data = readConfigObject(config);
  free(config);

  clock_t end = clock();
  float runtime = (float)(end - start) / 1000;

  if (logs) {
    char *message = malloc(128);
    sprintf(message, "Reading Config took %.1f ms", runtime);
    logger(3, message);
    free(message);
  }

  return config_data;
}

void writeConfig(const struct config *config_data, const bool save,
                 const bool skip, const char *units, const bool logs) {
  clock_t start = clock();

  /* Get old config and Create new config struct */
  const struct config *config_old = readConfig(logs);
  struct config *config_new = malloc(sizeof(struct config));

  /* Set new config depending on what is set in config data */
  config_new->geolocation = false;
  if (config_data->latitude == 0 && config_data->longitude == 0 &&
      config_data->country == NULL && config_data->city == NULL) {
    if (units != NULL) {
      config_new->latitude = config_old->latitude;
      config_new->longitude = config_old->longitude;
      config_new->country = config_old->country;
      config_new->city = config_old->city;
      config_new->geolocation = true;
      if (strcmp(units, "metric")) {
        config_new->units = true;
      } else if (strcmp(units, "imperial")) {
        config_new->units = false;
      } else {
        logger(1, "Invalid Unit Type");
        exit(1);
      }
    } else {
      config_new->latitude = config_old->latitude;
      config_new->longitude = config_old->longitude;
      config_new->country = config_old->country;
      config_new->city = config_old->city;
      config_new->units = config_old->units;
      config_new->geolocation = true;
    }
  } else {
    if (units != NULL) {
      config_new->latitude = config_data->latitude;
      config_new->longitude = config_data->longitude;
      config_new->country = config_data->country;
      config_new->city = config_data->city;
      if (strcmp(units, "metric")) {
        config_new->units = true;
      } else if (strcmp(units, "imperial")) {
        config_new->units = false;
      } else {
        logger(1, "Invalid Unit Type");
        exit(1);
      }
    } else {
      config_new->latitude = config_data->latitude;
      config_new->longitude = config_data->longitude;
      config_new->country = config_data->country;
      config_new->city = config_data->city;
      config_new->units = config_old->units;
    }
  }

  /* Saving to locations and skipping config generation */
  // if (save) {
  //
  // } else if (save && skip) {
  //   free(config_new);
  //   exit(0);
  // }

  char *full_path = getConfigPath(0);
  char *config_json = createConfigObject(config_new);

  struct stat buffer;
  int exists = stat(full_path, &buffer);
  if (exists != 0) {
    createConfig();
  }

  FILE *file = fopen(full_path, "w");
  fprintf(file, "%s", config_json);
  fclose(file);
  free(full_path);
  free(config_json);

  clock_t end = clock();
  float runtime = (float)(end - start) / 1000;

  if (logs) {
    char *message = malloc(128);
    sprintf(message, "Config Write took %.1f ms", runtime);
    logger(3, message);
    free(message);
  }
}

// int main() {
//   struct config *config_data = readConfig(true);
//   printf("%s\n", createConfigObject(config_data));
//   free(config_data);
// }
