#!/usr/bin/env bash

# Shell Script with functions for reading and
# writing config file

make_conf() {
  # Create default configuration file
  CONF_PATH="$HOME/.config/wayweather/"
  CONF_FILE="config.toml"

  echo -e "[\033[1;33mLOG\033[0m] Generating Config" >&2

  mkdir -p "$CONF_PATH"
  touch "$CONF_PATH$CONF_FILE"
  echo '# WayWeather config
# ip_loc enables/disables autolocation through ip address
# latitide, longitude, city and country must be set when ip_loc is false
# units can be either "imp" or "met" (imperial and metric)

[wayweather]
ip_loc = true
latitude = 0
longitude = 0
city = ""
country = ""
units = "met"' >"$CONF_PATH$CONF_FILE"
}

read_conf() {
  # Read config and return an array

  # Reading toml config
  declare -A CONF
  CONF_PATH="$HOME/.config/wayweather/config.toml"

  if [ ! -f "$CONF_PATH" ]; then
    make_conf
    # Create config if it does not exist
  fi

  echo -e "[\033[1;33mLOG\033[0m] Reading Config" >&2

  eval "$(tombl -e config=wayweather "$CONF_PATH")"
  if [ "${config[ip_loc]}" = "true" ]; then
    LOC=$(curl http://ip-api.com/json?fields=lat,lon,city,country -s | jq -c)
    CONF=(
      ["LAT"]="$(echo "$LOC" | jq -r '.lat' | awk '{printf "%.2f",$1}')"
      ["LONG"]="$(echo "$LOC" | jq -r '.lon' | awk '{printf "%.2f",$1}')"
      ["CITY"]="$(echo "$LOC" | jq -r '.city')"
      ["COUNTRY"]="$(echo "$LOC" | jq -r '.country')"
      ["UNITS"]="${config[units]}"
    )
    echo -e "[\033[1;33mLOG\033[0m] Using IP Geolocation" >&2
  else
    CONF=(
      ["LAT"]="${config[latitude]}"
      ["LONG"]="${config[longitude]}"
      ["CITY"]="${config[city]}"
      ["COUNTRY"]="${config[country]}"
      ["UNITS"]="${config[units]}"
    )
    echo -e "[\033[1;33mLOG\033[0m] Using Location in Config" >&2
  fi

  printf "%s " "${CONF[@]@K}"
}

write_conf() {
  CONF_PATH="$HOME/.config/wayweather/config.toml"

  if [ ! -f "$CONF_PATH" ]; then
    make_conf
    # Create config if it does not exist
  fi

  case $1 in
  set)
    eval "$(tombl -e config=wayweather "$CONF_PATH")"
    for i in "$@"; do
      case $i in
      lat=*)
        LAT="${i#*=}"
        ;;
      long=*)
        LONG="${i#*=}"
        ;;
      country=*)
        COUNTRY="${i#*=}"
        ;;
      city=*)
        CITY="${i#*=}"
        ;;
      units=*)
        UNITS="${i#*=}"
        ;;
      esac
    done

    if ! [[ -v LAT ]] && ! [[ -v LONG ]] && ! [[ -v COUNTRY ]] && ! [[ -v CITY ]]; then
      if [[ -v UNITS ]]; then
        LOCAT="false"
      elif [[ "${config[latitide]}" == "0" ]] && [[ "${config[longitude]}" == "0" ]] && [[ "${config[country]}" == "" ]] && [[ "${config[city]}" == "" ]]; then
        LOCAT="true"
      else
        LOCAT="false"
      fi
    else
      LOCAT="false"
    fi

    if [[ "$LOCAT" == "true" ]]; then
      echo -e "[\033[1;33mLOG\033[0m] Enabling IP Geolocation" >&2
    else
      echo -e "[\033[1;33mLOG\033[0m] Setting a Custom Location" >&2
    fi

    if ! [[ -v LAT ]]; then
      LAT="${config[latitude]}"
    fi
    if ! [[ -v LONG ]]; then
      LONG="${config[longitude]}"
    fi
    if ! [[ -v COUNTRY ]]; then
      COUNTRY="${config[country]}"
    fi
    if ! [[ -v CITY ]]; then
      CITY="${config[city]}"
    fi
    if ! [[ -v UNITS ]]; then
      UNITS="${config[units]}"
    fi
    echo "# WayWeather config
# ip_loc enables/disables autolocation through ip address
# latitide, longitude, city and country must be set when ip_loc is false
# units can be either \"imp\" or \"met\" (imperial and metric)

[wayweather]
ip_loc = $LOCAT
latitude = $LAT
longitude = $LONG
city = \"$CITY\"
country = \"$COUNTRY\"
units = \"$UNITS\"" >"$CONF_PATH"
    echo -e "[\033[1;33mLOG\033[0m] Config Generated" >&2
    ;;
  reset)
    eval "$(tombl -e config=wayweather "$CONF_PATH")"
    echo "# WayWeather config
# ip_loc enables/disables autolocation through ip address
# latitide, longitude, city and country must be set when ip_loc is false
# units can be either \"imp\" or \"met\" (imperial and metric)

[wayweather]
ip_loc = true
latitude = 0
longitude = 0
city = \"\"
country = \"\"
units = \"${config[units]}\"" >"$CONF_PATH"
    echo -e "[\033[1;33mLOG\033[0m] Config Reset to Default" >&2
    ;;
  esac
}
