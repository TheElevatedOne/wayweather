#!/usr/bin/env bash

# Shell Script that houses the parser
# for OpenMeteoAPI

api_pull() {
  LAT="$1"
  LONG="$2"
  CITY="$3"
  COUNTRY="$4"
  UNITS="$5"

  case $UNITS in
  imp)
    TEMP_U="&temperature_unit=fahrenheit"
    WIND_U="&wind_speed_unit=mph"
    PREC_U="&precipitation_unit=inch"
    ;;
  met)
    TEMP_U=""
    WIND_U=""
    PREC_U=""
    ;;
  esac

  WTHR_API=$(curl -s "https://api.open-meteo.com/v1/forecast?latitude=$LAT&longitude=$LONG&current=temperature_2m,relative_humidity_2m,precipitation,cloud_cover,wind_speed_10m,wind_direction_10m,wind_gusts_10m,is_day,weather_code&timezone=auto$TEMP_U$WIND_U$PREC_U" | jq -c)

  TIME="$(echo "$WTHR_API" | jq -r '.current.time' | awk '{split($1, time, "T"); printf "%s %s",time[1],time[2]}')"
  TEMP="$(echo "$WTHR_API" | jq -r '.current.temperature_2m')$(echo "$WTHR_API" | jq -r '.current_units.temperature_2m')"
  HUMID="$(echo "$WTHR_API" | jq -r '.current.relative_humidity_2m')%"
  PREC="$(echo "$WTHR_API" | jq -r '.current.precipitation' | awk '{printf "%.2f",$1}') $(echo "$WTHR_API" | jq -r '.current_units.precipitation')"
  CLOUD="$(echo "$WTHR_API" | jq -r '.current.cloud_cover')%"
  WIND_S="$(echo "$WTHR_API" | jq -r '.current.wind_speed_10m') $(echo "$WTHR_API" | jq -r '.current_units.wind_speed_10m' | awk '{if ($1 == "mp/h") {print "mph"} else {print $1}}')"
  WIND_G="$(echo "$WTHR_API" | jq -r '.current.wind_gusts_10m') $(echo "$WTHR_API" | jq -r '.current_units.wind_gusts_10m' | awk '{if ($1 == "mp/h") {print "mph"} else {print $1}}')"

  WIND_ARR=(
    "North"
    "North East"
    "East"
    "South East"
    "South"
    "South West"
    "West"
    "North West"
  )
  WIND_DIR="$(echo "$WTHR_API" | jq -r '.current.wind_direction_10m')"
  WIND_DIR_LET="${WIND_ARR[$(echo "$WIND_DIR" | awk '{print int(($1 / 45) % 8 )}')]}"

  IS_DAY="$(echo "$WTHR_API" | jq -r '.current.is_day')"
  WC="$(echo "$WTHR_API" | jq -r '.current.weather_code')"

  declare -A WC_LIST

  WC_LIST=(
    ["0_0"]="󰖔 "
    ["0_1"]="󰖙 "
    ["1_0"]="󰖔 "
    ["1_1"]="󰖙 "
    ["2"]="󰖐 "
    ["3"]="󰖐 "
    ["45"]="󰖑 "
    ["48"]="󰖑 "
    ["51"]="󰖗 "
    ["53"]="󰖗 "
    ["55"]="󰖗 "
    ["56"]="󰙿 "
    ["57"]="󰙿 "
    ["61"]="󰖖 "
    ["63"]="󰖖 "
    ["65"]="󰖖 "
    ["66"]="󰙿 "
    ["67"]="󰙿 "
    ["71"]="󰖘 "
    ["73"]="󰖘 "
    ["75"]="󰼶 "
    ["77"]="󰖒 "
    ["80"]="󰖖 "
    ["81"]="󰖖 "
    ["82"]="󰖖 "
    ["85"]="󰼶 "
    ["86"]="󰼶 "
    ["95"]="󰖓 "
    ["96"]="󰖓 "
    ["99"]="󰖓 "
  )

  WI="${WC_LIST["$(echo "$WC $IS_DAY" | awk '{if ($1 <= 1) {printf "%s_%s",$1,$2} else {printf "%s",$1}}')"]}"

  declare -A WTHR

  WTHR=(
    ["CITY"]="$CITY"
    ["COUNTRY"]="$COUNTRY"
    ["TIME"]="$TIME"
    ["TEMP"]="$TEMP"
    ["HUMID"]="$HUMID"
    ["PREC"]="$PREC"
    ["CLOUD"]="$CLOUD"
    ["WIND_S"]="$WIND_S"
    ["WIND_G"]="$WIND_G"
    ["WIND_DIR"]="$WIND_DIR_LET ($WIND_DIR°)"
    ["WI"]="$WI"
  )

  printf "%s " "${WTHR[@]@K}"
}

daemon() {
  SAVE_PATH="$HOME/.config/wayweather/locations.json"
  CONF_PATH="$HOME/.config/wayweather/config.toml"
  SLEEP="15"
  NO_ICON=false

  for i in "$@"; do
    case $i in
    no-icon)
      NO_ICON=true
      ;;
    location=*)
      LOCATION="${i#*=}"
      if [[ "$LOCATION" =~ ^[0-9]+$ ]]; then
        ID=false
        for key in $(cat "$SAVE_PATH" | jq -rc 'keys | @sh' | tr -d \'); do
          if [[ "$LOCATION" -eq "$key" ]]; then
            ID=true
          fi
        done
        if $ID; then
          load_loc "$LOCATION" >>/dev/null
        fi
      fi
      ;;
    timer=*)
      SLEEP="${i#*=}"
      ;;
    esac
  done

  while true; do
    unset HISTORY
    declare -A HISTORY
    HISTORY["0"]="NONE"
    HISTORY["1"]="NONE"
    HISTORY["2"]="NONE"
    UPDATE=false

    declare -A CONFIG="($(read_conf 2>/dev/null))"
    declare -A WTHR_ARR="($(api_pull "${CONFIG["LAT"]}" "${CONFIG["LONG"]}" "${CONFIG["CITY"]}" "${CONFIG["COUNTRY"]}" "${CONFIG["UNITS"]}" 2>/dev/null))"

    ICON="$(if $NO_ICON; then echo ""; else echo " <big>${WTHR_ARR["WI"]}</big>"; fi)"

    echo "{'text': '${WTHR_ARR["TEMP"]}$ICON'\
,'tooltip': '${WTHR_ARR["CITY"]}, ${WTHR_ARR["COUNTRY"]}\nTime: ${WTHR_ARR["TIME"]}\
\n\nTemperature: ${WTHR_ARR["TEMP"]}\nHumidity: ${WTHR_ARR["HUMID"]}\nPrecipitaion: ${WTHR_ARR["PREC"]}\
\nCloud Cover: ${WTHR_ARR["CLOUD"]}\nWind Direct: ${WTHR_ARR["WIND_DIR"]}\nWind Speed: ${WTHR_ARR["WIND_S"]}\nWind Gusts: ${WTHR_ARR["WIND_G"]}'}" | sed -e "s/'/\"/g" | jq -c

    while true; do
      HISTORY["0"]="$(cat "$CONF_PATH")"
      if [[ "${HISTORY["1"]}" != "NONE" && "${HISTORY["2"]}" != "NONE" ]] && [[ "${HISTORY["0"]}" != "${HISTORY["1"]}" || "${HISTORY["0"]}" != "${HISTORY["2"]}" || "${HISTORY["1"]}" != "${HISTORY["2"]}" ]]; then
        break
      fi
      sleep "$((SLEEP / 2))s"
      HISTORY["1"]="$(cat "$CONF_PATH")"
      if [[ "${HISTORY["2"]}" != "NONE" ]] && [[ "${HISTORY["0"]}" != "${HISTORY["1"]}" || "${HISTORY["0"]}" != "${HISTORY["2"]}" || "${HISTORY["1"]}" != "${HISTORY["2"]}" ]]; then
        break
      fi
      sleep "$((SLEEP / 2))s"
      HISTORY["2"]="$(cat "$CONF_PATH")"
      if [[ "${HISTORY["0"]}" != "${HISTORY["1"]}" || "${HISTORY["0"]}" != "${HISTORY["2"]}" || "${HISTORY["1"]}" != "${HISTORY["2"]}" ]]; then
        break
      fi
      for i in $(seq 0 4); do
        if [[ "$(($(date +"%M") - $((15 * i))))" == "0" ]]; then
          UPDATE=true
        fi
      done
      if $UPDATE; then
        break
      fi
    done
  done
}
