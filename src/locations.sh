#!/usr/bin/env bash

# Shell Script for Creating, Loading and
# Writing file with Custom Locations

make_loc() {
  SAVE_PATH="$HOME/.config/wayweather/locations.json"
  if [ ! -f "$SAVE_PATH" ]; then
    printf "{}" >"$SAVE_PATH"
    echo "[LOG] Created Custom Locations File"
  fi
}

load_loc() {
  make_loc
  SAVE_PATH="$HOME/.config/wayweather/locations.json"
  JSON="$(cat "$SAVE_PATH" | jq -c)"

  if [[ "$(echo "$JSON" | jq length)" -eq "0" ]]; then
    echo "[ERROR] No locations saved, exiting"
    echo "[INFO] Run --set --help for info on saving locations"
    exit 1
  fi

  DEFAULT=false
  NUMBER=false
  LOCATION_ID=""

  for i in "$@"; do
    case $i in
    default)
      DEFAULT=true
      ;;
    *)
      if [[ "$i" =~ ^[0-9]+$ ]]; then
        for key in $(echo "$JSON" | jq -rc 'keys | @sh' | tr -d \'); do
          if [[ "$i" -eq "$key" ]]; then
            NUMBER=true
            LOCATION_ID="$i"
          fi
        done
        if $NUMBER && $DEFAULT; then
          echo "[ERROR] Using Default and Location ID at the same time"
          echo "[LOG] Exiting."
          exit 1
        fi
        if ! $NUMBER; then
          echo "[WARNING] Invalid Location ID"
          echo "[LOG] Continuing normal execution"
        fi
      fi
      ;;
    esac
  done

  if ! $DEFAULT && ! $NUMBER; then
    for index in $(seq 0 "$(($(echo "$JSON" | jq length) - 1))"); do
      UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$index\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
      echo "[$index]\
$(if [[ "$(echo "$JSON" | jq ".[\"$index\"].default")" == "true" ]]; then echo "*"; else echo ""; fi) \
$(echo "$JSON" | jq ".[\"$index\"].city"), $(echo "$JSON" | jq ".[\"$index\"].country") \
($(echo "$JSON" | jq ".[\"$index\"].latitude"), $(echo "$JSON" | jq ".[\"$index\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
    done
    unset UNITS

    if [[ "$(echo "$JSON" | jq length)" -gt "1" ]]; then
      read -p "Enter a number [0-"$(($(echo "$JSON" | jq length) - 1))"]: " INPUT
      if ! [[ "$INPUT" =~ ^[0-9]+$ ]] || ! [[ "$INPUT" -ge "0" ]] || ! [[ "$INPUT" -le "$(($(echo "$JSON" | jq length) - 1))" ]]; then
        echo "[ERROR] Input Not a Valid Number! Exiting."
        exit 1
      fi
      echo "[LOG] Selecting Location ID $INPUT"
    else
      echo "[LOG] Selecting Location ID 0"
    fi
  elif $DEFAULT && ! $NUMBER; then
    readarray -t def <<<"$(echo "$JSON" | jq ".[].default")"

    for index in $(seq 0 "$((${#def[@]} - 1))"); do
      if [[ "${def[$index]}" == "true" ]]; then
        INPUT="$index"
      fi
    done
    if ! [[ -v INPUT ]]; then
      echo "[ERROR] Default Location Not Set! Exiting"
      exit 1
    else
      UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$INPUT\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
      echo "[LOG] Default Location Found"
      echo "[LOG] Location: \
$(echo "$JSON" | jq ".[\"$INPUT\"].city"), $(echo "$JSON" | jq ".[\"$INPUT\"].country") \
($(echo "$JSON" | jq ".[\"$INPUT\"].latitude"), $(echo "$JSON" | jq ".[\"$INPUT\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
      unset UNITS
    fi
  elif $NUMBER && ! $DEFAULT; then
    INPUT="$LOCATION_ID"
    UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$INPUT\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
    echo "[LOG] Location ID $INPUT: \
$(echo "$JSON" | jq ".[\"$INPUT\"].city"), $(echo "$JSON" | jq ".[\"$INPUT\"].country") \
($(echo "$JSON" | jq ".[\"$INPUT\"].latitude"), $(echo "$JSON" | jq ".[\"$INPUT\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
    unset UNITS
  fi

  LAT="$(echo "$JSON" | jq ".[\"$INPUT\"].latitude" | sed -e 's/"//g')"
  LONG="$(echo "$JSON" | jq ".[\"$INPUT\"].longitude" | sed -e 's/"//g')"
  CITY="$(echo "$JSON" | jq ".[\"$INPUT\"].city" | sed -e 's/"//g')"
  COUNTRY="$(echo "$JSON" | jq ".[\"$INPUT\"].country" | sed -e 's/"//g')"
  UNITS="$(echo "$JSON" | jq ".[\"$INPUT\"].units" | sed -e 's/"//g')"

  write_conf set "lat=$LAT" "long=$LONG" "city=$CITY" "country=$COUNTRY" "units=$UNITS"
}

write_loc() {
  make_loc
  SAVE_PATH="$HOME/.config/wayweather/locations.json"

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
    skip)
      SKIP=true
      ;;
    esac
  done

  JSON="{\"$(cat "$SAVE_PATH" | jq length)\":{
\"latitude\":$LAT,
\"longitude\":$LONG,
\"city\":\"$CITY\",
\"country\":\"$COUNTRY\",
\"units\":\"$UNITS\",
\"default\":false
}}"
  echo "[LOG] Saved location: $CITY, $COUNTRY ($LAT, $LONG) [$(if [[ "$UNITS" == "met" ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)]"
  # Add check for replacing with new confgigs
  SAVE="$(cat "$SAVE_PATH" | jq ". += $(echo "$JSON" | jq -c)")"
  echo "$SAVE" >"$SAVE_PATH"

  if [[ -v SKIP ]]; then
    exit 1
  fi
}

default_loc() {
  make_loc

  if [[ "$(echo "$JSON" | jq length)" -eq "0" ]]; then
    echo "[ERROR] No locations saved, exiting"
    echo "[INFO] Run --set --help for info on saving locations"
    exit 1
  fi

  SAVE_PATH="$HOME/.config/wayweather/locations.json"
  JSON="$(cat "$SAVE_PATH" | jq -c)"
  DEFAULT=""

  for index in $(seq 0 "$(($(echo "$JSON" | jq length) - 1))"); do
    UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$index\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
    echo "[$index]\
$(if [[ "$(echo "$JSON" | jq ".[\"$index\"].default")" == "true" ]]; then echo "*"; else echo ""; fi) \
$(echo "$JSON" | jq ".[\"$index\"].city"), $(echo "$JSON" | jq ".[\"$index\"].country") \
($(echo "$JSON" | jq ".[\"$index\"].latitude"), $(echo "$JSON" | jq ".[\"$index\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
    DEFAULT="$(if [[ "$(echo "$JSON" | jq ".[\"$index\"].default")" == "true" ]]; then echo "$index"; else echo "$DEFAULT"; fi)"
  done
  unset UNITS

  read -p "Enter a number [0-"$(($(echo "$JSON" | jq length) - 1))"]: " INPUT
  if ! [[ "$INPUT" =~ ^[0-9]+$ ]] || ! [[ "$INPUT" -ge "0" ]] || ! [[ "$INPUT" -le "$(($(echo "$JSON" | jq length) - 1))" ]]; then
    echo "[ERROR] Input Not a Valid Number! Exiting."
    exit 1
  fi

  if [[ "$INPUT" == "$DEFAULT" ]]; then
    echo "[LOG] Selected Location is already Default"
    echo "[LOG] Exiting"
    exit 0
  else
    if [[ "$DEFAULT" == "" ]]; then
      MOD="$(echo "$JSON" | jq ".[\"$INPUT\"].default = true")"
      echo "$MOD" >"$SAVE_PATH"
      echo "[LOG] Default Location Selected"
    else
      MOD="$(echo "$JSON" | jq ".[\"$INPUT\"].default = true" | jq ".[\"$DEFAULT\"].default = false")"
      echo "$MOD" >"$SAVE_PATH"
      echo "[LOG] Default Location Selected"
    fi
  fi
  unset JSON
  unset DEFAULT
}

list_loc() {
  make_loc
  SAVE_PATH="$HOME/.config/wayweather/locations.json"
  JSON="$(cat "$SAVE_PATH" | jq -c)"

  for index in $(seq 0 "$(($(echo "$JSON" | jq length) - 1))"); do
    UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$index\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
    echo "[$index]\
$(if [[ "$(echo "$JSON" | jq ".[\"$index\"].default")" == "true" ]]; then echo "*"; else echo ""; fi) \
$(echo "$JSON" | jq ".[\"$index\"].city"), $(echo "$JSON" | jq ".[\"$index\"].country") \
($(echo "$JSON" | jq ".[\"$index\"].latitude"), $(echo "$JSON" | jq ".[\"$index\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
  done
}

delete_loc() {
  make_loc
  SAVE_PATH="$HOME/.config/wayweather/locations.json"
  JSON="$(cat "$SAVE_PATH" | jq -c)"
  NOCONFIRM=false
  NUMBER=false

  if [[ "$(echo "$JSON" | jq length)" -eq "0" ]]; then
    echo "[ERROR] No locations saved, exiting"
    echo "[INFO] Run --set --help for info on saving locations"
    exit 1
  fi

  for i in "$@"; do
    case $i in
    yes)
      NOCONFIRM=true
      ;;
    *)
      if [[ "$i" =~ ^[0-9]+$ ]]; then
        for key in $(echo "$JSON" | jq -rc 'keys | @sh' | tr -d \'); do
          if [[ "$i" -eq "$key" ]]; then
            NUMBER=true
            LOCATION_ID="$i"
          fi
        done
        if ! $NUMBER; then
          echo "[WARNING] Invalid Location ID"
          echo "[LOG] Continuing normal execution"
        fi
      fi
      ;;
    esac
  done

  if $NUMBER; then
    if ! $NOCONFIRM; then
      read -p "Confirm Deletion of Location ID $LOCATION_ID [Y/n]: " CONFIRM
      case $CONFIRM in
      [yY]) ;;
      [nN])
        echo "[LOG] Operation Cancelled"
        exit 0
        ;;
      *)
        echo "[ERROR] Invalid Input"
        echo "[LOG] Operation Cancelled"
        exit 1
        ;;
      esac
    fi
    if [[ "$LOCATION_ID" -eq "$(($(echo "$JSON" | jq length) - 1))" ]]; then
      DELETE="$(echo "$JSON" | jq "del(.[\"$LOCATION_ID\"])")"
      printf "$DELETE" >"$SAVE_PATH"
    else
      DELETE="$(echo "$JSON" | jq "del(.[\"$LOCATION_ID\"])")"
      for id in $(seq "$((LOCATION_ID + 1))" "$(($(echo "$JSON" | jq length) - 1))"); do
        DELETE="$(echo "$DELETE" | jq -S ". += {\"$((id - 1))\": $(echo "$DELETE" | jq -c ".[\"$id\"]")}")"
        DELETE="$(echo "$DELETE" | jq "del(.[\"$id\"])")"
      done
      printf "$DELETE" >"$SAVE_PATH"
    fi
    UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$LOCATION_ID\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
    echo "[LOG] Deleted Location ID $LOCATION_ID: \
$(echo "$JSON" | jq ".[\"$LOCATION_ID\"].city"), $(echo "$JSON" | jq ".[\"$LOCATION_ID\"].country") \
($(echo "$JSON" | jq ".[\"$LOCATION_ID\"].latitude"), $(echo "$JSON" | jq ".[\"$LOCATION_ID\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
    unset units
  else
    for index in $(seq 0 "$(($(echo "$JSON" | jq length) - 1))"); do
      UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$index\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
      echo "[$index]\
$(if [[ "$(echo "$JSON" | jq ".[\"$index\"].default")" == "true" ]]; then echo "*"; else echo ""; fi) \
$(echo "$JSON" | jq ".[\"$index\"].city"), $(echo "$JSON" | jq ".[\"$index\"].country") \
($(echo "$JSON" | jq ".[\"$index\"].latitude"), $(echo "$JSON" | jq ".[\"$index\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
    done
    unset UNITS

    read -p "Enter a number [0-"$(($(echo "$JSON" | jq length) - 1))"]: " INPUT
    if ! [[ "$INPUT" =~ ^[0-9]+$ ]] || ! [[ "$INPUT" -ge "0" ]] || ! [[ "$INPUT" -le "$(($(echo "$JSON" | jq length) - 1))" ]]; then
      echo "[ERROR] Input Not a Valid Number! Exiting."
      exit 1
    fi
    LOCATION_ID="$INPUT"

    if ! $NOCONFIRM; then
      read -p "Confirm Deletion of Location ID $LOCATION_ID [Y/n]: " CONFIRM
      case $CONFIRM in
      [yY]) ;;
      [nN])
        echo "[LOG] Operation Cancelled"
        exit 0
        ;;
      *)
        echo "[ERROR] Invalid Input"
        echo "[LOG] Operation Cancelled"
        exit 1
        ;;
      esac
    fi

    if [[ "$LOCATION_ID" -eq "$(($(echo "$JSON" | jq length) - 1))" ]]; then
      DELETE="$(echo "$JSON" | jq "del(.[\"$LOCATION_ID\"])")"
      printf "$DELETE" >"$SAVE_PATH"
    else
      DELETE="$(echo "$JSON" | jq "del(.[\"$LOCATION_ID\"])")"
      for id in $(seq "$((LOCATION_ID + 1))" "$(($(echo "$JSON" | jq length) - 1))"); do
        DELETE="$(echo "$DELETE" | jq -S ". += {\"$((id - 1))\": $(echo "$DELETE" | jq -c ".[\"$id\"]")}")"
        DELETE="$(echo "$DELETE" | jq "del(.[\"$id\"])")"
      done
      printf "$DELETE" >"$SAVE_PATH"
    fi
    UNITS="$(if [[ $(echo "$JSON" | jq ".[\"$LOCATION_ID\"].units") == '"met"' ]]; then echo "°C, mm, km/h"; else echo "°F, inch, mph"; fi)"
    echo "[LOG] Deleted Location ID $LOCATION_ID: \
$(echo "$JSON" | jq ".[\"$LOCATION_ID\"].city"), $(echo "$JSON" | jq ".[\"$LOCATION_ID\"].country") \
($(echo "$JSON" | jq ".[\"$LOCATION_ID\"].latitude"), $(echo "$JSON" | jq ".[\"$LOCATION_ID\"].longitude")) \
[$UNITS]" | sed -e 's/"//g'
    unset units
  fi
}
