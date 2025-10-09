#!/usr/bin/env bash

# Shell Script with functions that write
# to stdout as their main function

show_help() {
  # Function for showing help

  for i in "$@"; do
    case $i in
    help)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-h/--help\033[0m] [\033[1;33m-h/--help\033[0m]

Not enough help? To wiki you go:
https://github.com/TheElevatedOne/wayweather/wiki'
      exit 0
      ;;
    main)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-h/--help\033[0m] [\033[1;32mOPTIONS\033[0m]

Weather script for Waybar with IP Geolocation

\033[1;32mHELP:\033[0m
    \033[1;33m-h, --help\033[0m            Print main help information
    \033[1;33m-v, --version\033[0m         Prints the version
    \033[1;33m<OPTION> -h, --help\033[0m   Print help information about
                          an option
    Example: > wayweather --get --help

\033[1;32mOPTIONS:\033[0m
    \033[1;33m-g, --get\033[0m [--no-icon]   Pring waybar json input
    \033[1;33m-s, --set\033[0m <ARGS>        Set custom location and
                            print waybar json input
    \033[1;33m-r, --reset\033[0m [--units]   Reset custom location to
                            IP geolocation
    \033[1;33m-p, --print\033[0m [--no-icon] Print waybar result to stdout
    \033[1;33m-l, --load\033[0m [OPTIONS]    Select locations from saved ones
        \033[1;33m--list\033[0m              List saved locations
    \033[1;33m-sd, --set-default\033[0m      Set default location for faster
                            loading
    \033[1;33m-d, --delete\033[0m [OPTIONS]  Delete locations from saved ones
    \033[1;33m-w, --daemon\033[0m [OPTIONS]  Runs a loop for checking config
                            changes prints waybar json when
                            a change occurs'
      ;;
    get)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-g/--get\033[0m] [\033[1;33m--no-icon\033[0m]

\033[1;32mOPTIONS:\033[0m
  \033[1;33m--no-icon\033[0m    Return a waybar parsable
               json string withou a NerdFont
               icon

Returns a compact JSON string for the waybar
module. It is not human readable.
Use with a restart interval.
'
      echo 'EXAMPLE:
  > wayweather --get
  {"text":"71.1°F <big>󰖔 </big>","tooltip":"New York, USA\nTime: 2025-09-30 19:45\n\n
  Temperature: 71.1°F\nHumidity: 53%\nPrecipitaion: 0.00 inch\nCloud Cover: 19%\n
  Wind Direct: North West (356°)\nWind Speed: 6.3 mph\nWind Gusts: 15.2 mph"}'
      exit 0
      ;;
    set)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-s/--set\033[0m] [\033[1;32mOPTIONS\033[0m]

\033[1;32mOPTIONS:\033[0m
  Arguments for setting custom location
  Enclose in double quotes (") for expected result

  \033[36mINFO:\033[0m Aruments --lat, --long, --city, --country must
  be used together, --units may be used separately

  \033[36mINFO:\033[0m When Saving a Custom Location, ALL arguments
  must be used. --skip-config is optional.

    \033[1;33m--lat=\033[0mLATITUDE      Latitude in decimal format
                        with two decimal places
    \033[1;33m--long=\033[0mLONGITUDE    Longitude in decimal format
                        with two decimal places
    \033[1;33m--city=\033[0mCITY         City name
    \033[1;33m--country=\033[0mCOUNTRY   Country name
    \033[1;33m--units=\033[0mUNITS       Either "met" or "imp" for
                        metric and imperial units
    \033[1;33m--save\033[0m              Save custom location for
                        future use. Use with all
                        setting arguments.
    \033[1;33m--skip-config\033[0m       Skip config generation
                        when saving custom location

EXAMPLE:
  > wayweather --set --lat="40.71" --long="-74.01" --city="New York" --country="USA" --units="imp" --save
  [LOG] Saved location: New York, USA (40.71, -74.01) [°F, inch, mph]
  [LOG] Config Generated'
      exit 0
      ;;
    reset)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-r/--reset\033[0m [\033[1;33m-u/--units\033[0m]

\033[1;32mOPTIONS:\033[0m
  \033[1;33m-u, --units\033[0m      Reset units when reseting the location

When setting a location via wayweather, it is saved to
the config file (at $HOME/.config/wayweather/config.toml).

This option will reset location information, but ignore
units, unless specified.'
      exit 0
      ;;
    load)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-l/--load\033[0m] [\033[1;33mdefault\033[0m]

\033[1;32mOPTIONS:\033[0m
  \033[1;33mdefault\033[0m        Just the string "default"
                 Loads the default Custom Location
  \033[1;33mID\033[0m             Expects a number (ID) of a saved
                 location
                 Get locations with --list

Loads Custom Locations from the locations file. If there
is more than one saved location, a selector will appear,
otherwise the location will be selected automatically.
When using "default", it will load the default location,
set with -sd/--set-default.
ID and default cannot be used at the same time.'
      exit 0
      ;;
    print)
      echo -e "> \033[1;31mwayweather\033[0m [\033[1;33m-p/--print\033[0m] [\033[1;33m--no-icon\033[0m]

\033[1;32mOPTIONS:\033[0m
  \033[1;33m--no-icon\033[0m       Disables the weather icon
                  from NerdFonts

Prints the waybar result to stdout in a human
readable format.
For testing purposes."
      exit 0
      ;;
    set-default)
      echo -e '> \033[1;31mwayweather\033[0m [\033[1;33m-sd/--set-default\033[0m]

Lets you set a default location from
saved ones. Useful for "--load default"'
      exit 0
      ;;
    delete)
      echo -e "> \033[1;31mwayweather\033[0m [\033[1;33m-d/--delete\033[0m] [\033[1;32mOPTIONS\033[0m]

\033[1;32mOPTIONS:\033[0m
  \033[1;33m-y, --yes\033[0m       Auto confirms deletion of a
                  location
  \033[1;33mID\033[0m              Expects a location ID from --list
                  Will delete specific location

Deletes saved locations via either an argument (ID)
or via a selection.
By default, deleting requires a confirmation (Y/n).
Adding argument --yes skips the confirmation and
auto accepts it."
      exit 0
      ;;
    daemon)
      echo -e "> \033[1;31mwayweather\033[0m [\033[1;33m-w/--daemon\033[0m] [\033[1;33mOPTIONS\033[0m]

\033[1;32mOPTIONS:\033[0m
  \033[1;33m--no-icon\033[0m       Disables the weather icon
                  from NerdFonts
  \033[1;33m--location=NUM\033[0m  Expects a location ID from --list
                  Will load the location into
                  config and use it
  \033[1;33m--timer=NUM\033[0m     Expects the amount of seconds
                  the daemon should wait before
                  checking changes in config
                  Default: 15, Recommended: 5 - 30

Runs a loop with the same components as --get but
checks changes in the config so the API is not
polled every time.
The check is done in three parts, so that is has
redundancy and has a 15 minute timer for API
updates.
Returns a stream of waybar parsable JSON strings."
      exit 0
      ;;
    list)
      echo -e "> \033[1;31mwayweather\033[0m [\033[1;33m--list\033[0m]

Lists saved locations.
Borrows code from --load."
      exit 0
      ;;
    esac
  done
}

waybar_return() {
  # Prints a json string that waybar can parse
  NO_ICON=false

  declare -A CONFIG="($(read_conf))"
  declare -A WTHR_ARR="($(api_pull "${CONFIG["LAT"]}" "${CONFIG["LONG"]}" "${CONFIG["CITY"]}" "${CONFIG["COUNTRY"]}" "${CONFIG["UNITS"]}"))"

  for i in "$@"; do
    case $i in
    no-icon)
      NO_ICON=true
      ;;
    esac
  done

  ICON="$(if $NO_ICON; then echo ""; else echo " <big>${WTHR_ARR["WI"]}</big>"; fi)"

  echo "{'text': '${WTHR_ARR["TEMP"]}$ICON'\
,'tooltip': '${WTHR_ARR["CITY"]}, ${WTHR_ARR["COUNTRY"]}\nTime: ${WTHR_ARR["TIME"]}\
\n\nTemperature: ${WTHR_ARR["TEMP"]}\nHumidity: ${WTHR_ARR["HUMID"]}\nPrecipitaion: ${WTHR_ARR["PREC"]}\
\nCloud Cover: ${WTHR_ARR["CLOUD"]}\nWind Direct: ${WTHR_ARR["WIND_DIR"]}\nWind Speed: ${WTHR_ARR["WIND_S"]}\nWind Gusts: ${WTHR_ARR["WIND_G"]}'}" | sed -e "s/'/\"/g" | jq -c
}

term_return() {
  # Prints the waybar result in a human readable
  # format to stdout
  NO_ICON=false

  declare -A CONFIG="($(read_conf))"
  declare -A WTHR_ARR="($(api_pull "${CONFIG["LAT"]}" "${CONFIG["LONG"]}" "${CONFIG["CITY"]}" "${CONFIG["COUNTRY"]}" "${CONFIG["UNITS"]}"))"

  for i in "$@"; do
    case $i in
    no-icon)
      NO_ICON=true
      ;;
    esac
  done

  ICON="$(if $NO_ICON; then echo ""; else echo " ${WTHR_ARR["WI"]}"; fi)"

  echo "TEXT:
${WTHR_ARR["TEMP"]}$ICON

TOOLTIP:
${WTHR_ARR["CITY"]}, ${WTHR_ARR["COUNTRY"]}
Time: ${WTHR_ARR["TIME"]}

Temperature: ${WTHR_ARR["TEMP"]}
Humidity: ${WTHR_ARR["HUMID"]}
Precipitaion: ${WTHR_ARR["PREC"]}
Cloud Cover: ${WTHR_ARR["CLOUD"]}
Wind Direct: ${WTHR_ARR["WIND_DIR"]}
Wind Speed: ${WTHR_ARR["WIND_S"]}
Wind Gusts: ${WTHR_ARR["WIND_G"]}"
}
