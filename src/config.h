#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdbool.h>

/* Values for config file */
struct config {
  bool geolocation;
  bool units;
  float latitude;
  float longitude;
  char *city;
  char *country;
};

/* Reset Config
 * Units can be "metric" and "imperial" */
void resetConfig(const bool logs, const char *units);

/* Reads current config into a config struct */
struct config *readConfig(bool logs);

/* Expects a config struct, if you want to save the location,
 * If you are skipping and want to skip config generation,
 * Units can be "metric" and "imperial" */
void writeConfig(const struct config *config_data, const bool save,
                 const bool skip, const char *units, const bool logs);

#endif // !CONFIG_H_
