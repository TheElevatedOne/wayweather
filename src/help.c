#include <stdio.h>

void main_help() {
  printf(
      "> \033[1;31mwayweather\033[0m [\033[1;33m-h/--help\033[0m] "
      "[\033[1;32mOPTIONS\033[0m]\n\nWeather script for Waybar with IP "
      "Geolocation\n\n\033[1;32mHELP:\033[0m\n    \033[1;33m-h, --help\033[0m  "
      "          Print main help information\n    \033[1;33m-v, "
      "--version\033[0m         Prints the version\n    "
      "\033[1;33m--verbose\033[0m             "
      "Shows logs about things\n                          happening in the "
      "background\n    \033[1;33m<OPTION> -h, "
      "--help\033[0m   Print help information about\n                          "
      "an option\n    Example: > wayweather --get "
      "--help\n\n\n\033[1;32mOPTIONS:\033[0m\n    \033[1;33m-g, --get\033[0m "
      "[--no-icon]   Pring waybar json input\n    \033[1;33m-s, --set\033[0m "
      "<ARGS>        Set custom location and\n                            "
      "print waybar json input\n    \033[1;33m-r, --reset\033[0m [--units]   "
      "Reset custom location to\n                            IP geolocation\n  "
      "  \033[1;33m-p, --print\033[0m [--no-icon] Print waybar result to "
      "stdout\n    \033[1;33m-l, --load\033[0m [OPTIONS]    Select locations "
      "from saved ones\n        \033[1;33m--list\033[0m              List "
      "saved locations\n    \033[1;33m-sd, --set-default\033[0m      Set "
      "default location for faster\n                            loading\n    "
      "\033[1;33m-d, --delete\033[0m [OPTIONS]  Delete locations from saved "
      "ones\n    \033[1;33m-w, --daemon\033[0m [OPTIONS]  Runs a loop for "
      "checking config\n                            changes prints waybar json "
      "when\n                            a change occurs\n \n");
}

void extra_help() {
  printf("> \033[1;31mwayweather\033[0m [\033[1;33m-h/--help\033[0m] "
         "[\033[1;33m-h/--help\033[0m]\n\nNot enough help? To wiki you "
         "go:\n\033[4;36mhttps://github.com/TheElevatedOne/wayweather/"
         "wiki\033[0m\n");
}

void get_help() {
  printf(
      "> \033[1;31mwayweather\033[0m [\033[1;33m-g/--get\033[0m] "
      "[\033[1;33m--no-icon\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  "
      "\033[1;33m--no-icon\033[0m    Return a waybar parsable\n             "
      "  json string withou a NerdFont\n               icon\n\nReturns a "
      "compact JSON string for the waybar\nmodule. It is not human "
      "readable.\nUse with a restart interval.\n\nEXAMPLE:\n  > wayweather "
      "--get\n  {\"text\":\"71.1°F <big>󰖔 </big>\",\"tooltip\":\"New York, "
      "USA\\nTime: 2025-09-30 19:45\\n\\n\n  Temperature: 71.1°F\\nHumidity: "
      "53\%\\nPrecipitaion: 0.00 inch\\nCloud Cover: 19\%\\n\n  Wind Direct: "
      "North West (356°)\\nWind Speed: 6.3 mph\\nWind Gusts: 15.2 mph\"}\n");
}

void set_help() {
  printf("> \033[1;31mwayweather\033[0m [\033[1;33m-s/--set\033[0m] "
         "[\033[1;32mOPTIONS\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  Arguments "
         "for setting custom location\n  Enclose in double quotes (\") for "
         "expected result\n\n  \033[36mINFO:\033[0m Aruments --lat, --long, "
         "--city, --country must\n  be used together, --units may be used "
         "separately\n\n  \033[36mINFO:\033[0m When Saving a Custom Location, "
         "ALL arguments\n  must be used. --skip-config is optional.\n\n    "
         "\033[1;33m--lat=\033[0mLATITUDE      Latitude in decimal format\n    "
         "                    with two decimal places\n    "
         "\033[1;33m--long=\033[0mLONGITUDE    Longitude in decimal format\n   "
         "                     with two decimal places\n    "
         "\033[1;33m--city=\033[0mCITY         City name\n    "
         "\033[1;33m--country=\033[0mCOUNTRY   Country name\n    "
         "\033[1;33m--units=\033[0mUNITS       Either \"met\" or \"imp\" for\n "
         "                       metric and imperial units\n    "
         "\033[1;33m--save\033[0m              Save custom location for\n      "
         "                  future use. Use with all\n                        "
         "setting arguments.\n    \033[1;33m--skip-config\033[0m       Skip "
         "config generation\n                        when saving custom "
         "location\n\nEXAMPLE:\n  > wayweather --set --lat=\"40.71\" "
         "--long=\"-74.01\" --city=\"New York\" --country=\"USA\" "
         "--units=\"imp\" --save\n  [LOG] Saved location: New York, USA "
         "(40.71, -74.01) [°F, inch, mph]\n  [LOG] Config Generated'\n");
}

void reset_help() {
  printf(
      "> \033[1;31mwayweather\033[0m [\033[1;33m-r/--reset\033[0m "
      "[\033[1;33m-u/--units\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  "
      "\033[1;33m-u, --units\033[0m      Reset units when reseting the "
      "location\n\nWhen setting a location via wayweather, it is saved to\nthe "
      "config file (at $HOME/.config/wayweather/config.toml).\n\nThis option "
      "will reset location information, but ignore\nunits, unless specified\n");
}

void load_help() {
  printf("> \033[1;31mwayweather\033[0m [\033[1;33m-l/--load\033[0m] "
         "[\033[1;33mdefault\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  "
         "\033[1;33mdefault\033[0m        Just the string \"default\"\n        "
         "         Loads the default Custom Location\n  \033[1;33mID\033[0m    "
         "         Expects a number (ID) of a saved\n                 "
         "location\n                 Get locations with --list\n\nLoads Custom "
         "Locations from the locations file. If there\nis more than one saved "
         "location, a selector will appear,\notherwise the location will be "
         "selected automatically.\nWhen using \"default\", it will load the "
         "default location,\nset with -sd/--set-default.\nID and default "
         "cannot be used at the same time.\n");
}

void print_help() {
  printf("> \033[1;31mwayweather\033[0m [\033[1;33m-p/--print\033[0m] "
         "[\033[1;33m--no-icon\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  "
         "\033[1;33m--no-icon\033[0m       Disables the weather icon\n         "
         "         from NerdFonts\n\nPrints the waybar result to stdout in a "
         "human\nreadable format.\nFor testing purposes.\n");
}

void default_help() {
  printf("> \033[1;31mwayweather\033[0m "
         "[\033[1;33m-sd/--set-default\033[0m]\n\nLets you set a default "
         "location from\nsaved ones. Useful for \"--load default\"\n");
}

void delete_help() {
  printf("> \033[1;31mwayweather\033[0m [\033[1;33m-d/--delete\033[0m] "
         "[\033[1;32mOPTIONS\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  "
         "\033[1;33m-y, --yes\033[0m       Auto confirms deletion of a\n       "
         "           location\n  \033[1;33mID\033[0m              Expects a "
         "location ID from --list\n                  Will delete specific "
         "location\n\nDeletes saved locations via either an argument (ID)\nor "
         "via a selection.\nBy default, deleting requires a confirmation "
         "(Y/n).\nAdding argument --yes skips the confirmation and\nauto "
         "accepts it.\n");
}

void daemon_help() {
  printf(
      "> \033[1;31mwayweather\033[0m [\033[1;33m-w/--daemon\033[0m] "
      "[\033[1;33mOPTIONS\033[0m]\n\n\033[1;32mOPTIONS:\033[0m\n  "
      "\033[1;33m--no-icon\033[0m       Disables the weather icon\n            "
      "      from NerdFonts\n  \033[1;33m--location=NUM\033[0m  Expects a "
      "location ID from --list\n                  Will load the location "
      "into\n                  config and use it\n  "
      "\033[1;33m--timer=NUM\033[0m     Expects the amount of seconds\n        "
      "          the daemon should wait before\n                  checking "
      "changes in config\n                  Default: 15, Recommended: 5 - "
      "30\n\nRuns a loop with the same components as --get but\nchecks changes "
      "in the config so the API is not\npolled every time.\nThe check is done "
      "in three parts, so that is has\nredundancy and has a 15 minute timer "
      "for API\nupdates.\nReturns a stream of waybar parsable JSON strings.\n");
}

void list_help() {
  printf("> \033[1;31mwayweather\033[0m [\033[1;33m--list\033[0m]\n\nLists "
         "saved locations.\n");
}
