#!/bin/sh
# Offline smoke tests for the wayweather CLI. Never touches the real config.
# Usage: tests/smoke.sh path/to/wayweather

set -u
BIN=${1:-./dist/wayweather}
case $BIN in /*) ;; *) BIN=$(pwd)/$BIN ;; esac

SANDBOX=$(pwd)/build/test-sandbox
rm -rf "$SANDBOX"
mkdir -p "$SANDBOX"
export XDG_CONFIG_HOME="$SANDBOX/config" XDG_CACHE_HOME="$SANDBOX/cache" \
       XDG_RUNTIME_DIR="$SANDBOX/run" NO_COLOR=1
CFG="$SANDBOX/config.json"
ww() { "$BIN" -c "$CFG" "$@"; }

pass=0 fail=0
check() { # description, command...
  desc=$1; shift
  if out=$("$@" 2>&1); then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n%s\n' "$desc" "$out" | sed 's/^/        /'
  fi
}
contains() { # needle, command...
  needle=$1; shift
  "$@" 2>&1 | grep -qF -- "$needle"
}
fails() { ! "$@" >/dev/null 2>&1; }

echo "wayweather smoke tests ($BIN)"

check "version"                 contains "wayweather" ww --version
check "help lists commands"     contains "Commands:" ww help
check "command help"            contains "Usage: wayweather add" ww add --help
check "format help"             contains "{wind_arrow}" ww help format
check "unknown command fails"   fails ww frobnicate
check "config is created"       ww config check
check "empty list"              contains "No saved locations" ww list

check "add by coordinates"      ww add --lat 48.1486 --lon 17.1077 --city Bratislava --country Slovakia
check "add with name+default"   ww add --lat 50.0755 --lon 14.4378 --city Prague --name Work --default
check "add imperial"            ww add --lat 40.7128 --lon -74.006 --city "New York" -u imperial
check "duplicate is rejected"   fails ww add --lat 48.1486 --lon 17.1077 --city Again
check "bad latitude rejected"   fails ww add --lat 123 --lon 0 --city Nowhere
check "list shows 3"            contains "New York" ww list
check "list --json is JSON"     sh -c "\"$BIN\" -c \"$CFG\" list --json | python3 -c 'import json,sys; assert len(json.load(sys.stdin)) == 3'"

check "use by name"             contains "Now showing: Work" ww use work
check "next cycles"             contains "New York" ww next
check "next reaches geo"        contains "Geolocation" ww next
check "prev goes back"          contains "New York" ww prev
check "use by index"            contains "Bratislava" ww use 1
check "default shows Work"      contains "#2 Work" ww default
check "default can be set"      contains "#3" ww default 3
check "default --clear"         contains "cleared" ww default --clear

check "units preset + override" contains "°F, mph, in, hPa, 12h" ww units imperial --pressure hpa
check "units synced to saved"   contains "Bratislava, Slovakia  (48.1486, 17.1077 · °F" ww list
check "units of saved loc"      contains "kn" ww units -l 2 --wind kn
check "invalid unit fails"      fails ww units --wind lightyears
check "edit renames"            contains "Updated #1: Home" ww edit 1 --name Home
check "edit by name"            contains "°C" ww edit home -u metric
check "edit nothing fails"      fails ww edit 1
check "ambiguous ref fails"     fails ww use o
check "remove needs --yes"      fails ww remove 3 </dev/null
check "remove"                  contains "Deleted" ww remove 3 -y
check "set custom location"     contains "Current location" ww set --lat 1.5 --lon 2.5 --city Atlantis
check "set --save adds"         contains "Saved as #3" ww set --lat 3 --lon 4 --city Lemuria --save
check "geo provider"            contains "ip" ww geo provider ip
check "geo on/off"              contains "disabled" ww geo off
check "reset enables geo"       contains "geolocation enabled" ww reset
check "config show"             contains "\"version\":" ww config show
check "legacy --list alias"     contains "Lemuria" ww --list
check "legacy -sd alias"        contains "No default" ww -sd

cp "$CFG" "$SANDBOX/good.json"
printf '{ "version": 3, broken' >"$CFG"
check "broken config reported"  fails ww list
check "get prints error JSON"   sh -c "\"$BIN\" -c \"$CFG\" get 2>/dev/null | grep -q '\"class\":\\[\"error\"\\]'"
cp "$SANDBOX/good.json" "$CFG"

# Legacy migration (Bash-era config.toml + locations.json)
mkdir -p "$XDG_CONFIG_HOME/wayweather"
cat >"$XDG_CONFIG_HOME/wayweather/config.toml" <<'TOML'
[wayweather]
ip_loc = false
latitude = 51.51
longitude = -0.13
city = "London"
country = "UK"
units = "imp"
TOML
printf '{"0":{"latitude":52.52,"longitude":13.40,"city":"Berlin","country":"Germany","units":"met","default":true}}' \
  >"$XDG_CONFIG_HOME/wayweather/locations.json"
check "legacy config migrated"  contains "Berlin" "$BIN" list
check "legacy units kept"       contains "°F" "$BIN" units
check "legacy default kept"     contains "Berlin" "$BIN" default

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
