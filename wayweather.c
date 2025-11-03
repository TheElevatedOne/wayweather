#include "src/api-parser.h"
#include "src/config.h"
#include "src/geolocation.h"
#include "src/help.h"
#include "src/logging.h"
#include "src/return.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {

  const char *argument = argv[1];

  if (argc == 1) {
    main_help();
    return 0;
  }

  bool logging = false;
  for (int i; i < argc; i++) {
    if (strcmp(argv[i], "--verbose")) {
      logging = true;
    }
  }

  /* Help */
  if ((strcmp(argument, "-h") == 0) | (strcmp(argument, "--help") == 0)) {
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 | strcmp(argv[i], "--help") == 0) {
        extra_help();
        return 0;
      }
    }
    main_help();
    return 0;
    /* } else if ((strcmp(argument, "-v") == 0) |
               (strcmp(argument, "--version") == 0)) {
      // Version
      return 0; */
    /* Get Waybar */
  } else if ((strcmp(argument, "-g") == 0) | (strcmp(argument, "--get") == 0)) {
    bool no_icon = false;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 | strcmp(argv[i], "--help") == 0) {
        get_help();
        exit(0);
      } else if (strcmp(argv[i], "--no-icon") == 0) {
        no_icon = true;
      }
    }
    /* Prepare structs */
    struct config *config_data = malloc(sizeof(struct config));

    /* Allocate Structs */
    config_data = readConfig(logging);
    bool units = config_data->units;
    if (config_data->geolocation) {
      config_data = locateIP(logging);
      config_data->units = units;
    }
    struct data *meteo_data =
        getMeteoData(config_data->latitude, config_data->longitude,
                     config_data->units, logging);
    waybar(meteo_data, config_data, no_icon);
    return 0;
    // waybar_return(no_icon);
    /* } else if ((strcmp(argument, "-s") == 0) | (strcmp(argument, "--set") ==
      0)) { return 0; */
  } else if ((strcmp(argument, "-r") == 0) |
             (strcmp(argument, "--reset") == 0)) {
    bool units = false;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 | strcmp(argv[i], "--help") == 0) {
        reset_help();
        break;
      } else if (strcmp(argv[i], "-u") == 0 | strcmp(argv[i], "--units") == 0) {
        units = true;
      }
    }
    return 0;
    // reset stuff
  } else if ((strcmp(argument, "-p") == 0) |
             (strcmp(argument, "--print") == 0)) {
    bool no_icon = false;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 | strcmp(argv[i], "--help") == 0) {
        get_help();
        break;
      } else if (strcmp(argv[i], "--no-icon") == 0) {
        no_icon = true;
      }
    }
    return 0;
    // term_return(no_icon);
    /* } else if ((strcmp(argument, "-l") == 0) |
               (strcmp(argument, "--load") == 0)) {
      return 0; */
  } else if ((strcmp(argument, "--list") == 0)) {
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 | strcmp(argv[i], "--help") == 0) {
        list_help();
      }
    }
    // list
    return 0;
  } else if ((strcmp(argument, "-sd") == 0) |
             (strcmp(argument, "--set-default") == 0)) {
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 | strcmp(argv[i], "--help") == 0) {
        list_help();
      }
    }
    // default
    return 0;
    /* } else if ((strcmp(argument, "-d") == 0) |
               (strcmp(argument, "--delete") == 0)) {
      return 0;
    } else if ((strcmp(argument, "-w") == 0) |
               (strcmp(argument, "--daemon") == 0)) {
      return 0; */
  } else {
    fprintf(stderr,
            "[\033[1;31mERROR\033[0m] Invalid argument "
            "supplied.\n[\033[1;33mLOG\033[0m] Showing help message\n\n");
    main_help();
    return 1;
  }
}
