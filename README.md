# WayWeather

Weather module for [Waybar](https://github.com/Alexays/Waybar), written in C,
powered by [Open-Meteo](https://open-meteo.com) (no API key needed).

- Current weather in the bar, with a rich tooltip: hourly and daily forecasts,
  wind, pressure, UV, sunrise/sunset
- **Saved locations**, each with its own measurements (°C/°F, km/h, m/s, mph,
  knots, mm/inch, hPa/inHg/mmHg/kPa, 24h/12h)
- **Geolocation** overrides the current location: GPS via gpsd, GeoClue2, or
  IP, tried in that order
- **TUI** to search cities, then add, edit, delete (with undo), reorder and
  switch locations, plus all settings with a live preview
- **Daemon** that updates the bar as soon as you change something, works
  offline from its cache, and reacts to clicks (`next` / `prev` / `refresh`)
- Bar text you can template, CSS classes per condition, day/night and
  temperature
- **libwayweather**: the weather, place search and geolocation as a C
  library (`wayweather.h`, `.so` + `.a`, pkg-config) for your own programs
- Completions for bash, zsh and fish, a man page, and automatic migration of
  2.x configs

[![AUR Version](https://img.shields.io/aur/version/wayweather?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/wayweather)

## Sections

- [**Installation**](#installation)
- [**Waybar module**](#waybar-module)
- [**Usage**](#usage)
- [**TUI**](#tui)
- [**Config**](#config)
- [**Geolocation**](#geolocation)
- [**C library**](#c-library)

## Installation

**Dependencies:** `libcurl`, `cJSON`, `ncursesw`, and optionally
`libgeoclue-2.0`. You also need a [Nerd Font](https://www.nerdfonts.com/) for
the icons.

| Distro        | Packages                                                     |
| ------------- | ------------------------------------------------------------ |
| Arch          | `curl cjson ncurses geoclue`                                 |
| Debian/Ubuntu | `libcurl4-openssl-dev libcjson-dev libncurses-dev libgeoclue-2-dev pkg-config` |
| Fedora        | `libcurl-devel cjson-devel ncurses-devel geoclue2-devel`     |

1. **AUR**: `yay -S wayweather` / `paru -S wayweather`
2. **From source**:

   ```sh
   git clone https://github.com/TheElevatedOne/wayweather.git
   cd wayweather
   make                 # -> dist/wayweather, dist/libwayweather.{so,a}
   make check           # offline CLI and library tests
   sudo make install    # or: make install-user  (~/.local)
   ```

   | Target                        | What it does                                   |
   | ----------------------------- | ---------------------------------------------- |
   | `make` / `make release`       | binary and libraries in `dist/`                |
   | `make lib`                    | only `libwayweather.so` / `.a`                 |
   | `make debug`                  | ASan/UBSan build in `build/debug/`             |
   | `make install` / `uninstall`  | honours `PREFIX` (default `/usr/local`) and `DESTDIR` |
   | `make package`                | release tarball (binary, libraries, header)    |
   | `make clean` / `distclean`    | remove `build/` (and `dist/`)                  |
   | `GEOCLUE=0`                   | build without GeoClue (auto-detected)          |

Sources live in `src/`, objects and dependency files in `build/`, and the
binary and libraries go to `dist/`. The version comes from `VERSION`.

## Waybar module

Recommended setup, using the daemon:

```jsonc
"custom/weather": {
  "exec": "wayweather daemon",
  "return-type": "json",
  "restart-interval": 10,
  "on-click": "wayweather next",
  "on-click-right": "wayweather prev",
  "on-click-middle": "wayweather refresh"
}
```

The daemon redraws immediately whenever the config changes, whether from the
TUI, `wayweather next`, or an editor. It refetches every `refresh_interval`
seconds. When the network is down it keeps showing cached data, adds the
`stale` class, and retries with backoff.

To poll instead, use `"exec": "wayweather get"` with `"interval": 900`.

Example config and CSS: [`data/waybar/`](data/waybar). The CSS uses your
theme's named colours (pywal's `@foreground`, `@color1`, …) rather than
fixed ones. Classes on every
update:

| Kind        | Classes                                                                 |
| ----------- | ----------------------------------------------------------------------- |
| condition   | `clear` `partly-cloudy` `cloudy` `fog` `drizzle` `rain` `freezing` `snow` `thunder` |
| time of day | `day` `night`                                                           |
| temperature | `hot` `warm` `mild` `cool` `cold`                                       |
| state       | `stale` `geolocated` `error`                                            |

`alt` holds the condition (handy for `format-icons`). `percentage` holds the
cloud cover.

## Usage

```
wayweather [GLOBAL OPTIONS] COMMAND [ARGS]

Commands:
  get       Print Waybar JSON once (use with "interval")
  print     Show a detailed report with forecast in the terminal
  daemon    Stream Waybar JSON, updating on config changes
  tui       Interactive manager for locations, units and settings
  search    Search places by name
  add       Save a new location
  list      List saved locations
  remove    Delete saved locations
  edit      Change a saved location
  use       Show a saved location (or geolocation)
  default   Show or set the default location
  next      Switch to the next saved location
  prev      Switch to the previous saved location
  set       Set the current location manually
  units     Show or change measurements
  geo       Control GPS / IP geolocation
  refresh   Make running daemons refetch now
  reset     Reset the current location
  config    Inspect or edit the config file
  help      Show help for a command
```

`LOC` is a number from `wayweather list`, a name, a city, or `default`.
`wayweather COMMAND --help` explains each command.

```sh
wayweather search "Springfield, Illinois"     # comma narrows by region/country
wayweather add Vienna --name Work --default   # pick from matches in a terminal
wayweather add --lat 64.15 --lon -21.94 --city Reykjavík -u imperial
wayweather use work                           # show it in Waybar
wayweather units imperial --pressure hpa      # preset + override
wayweather units -l 2 --wind kn               # units of a saved location
wayweather print -l Reykjavík                 # forecast in the terminal
wayweather geo on                             # follow GPS / IP position
wayweather geo save Cabin                     # save where you are now
```

The 2.x flags still work: `--get`, `--print`, `--set`, `--load`, `--list`,
`--set-default`, `--delete`, `--daemon` and `--reset`.

## TUI

Run `wayweather tui`, or just `wayweather` in a terminal.

- **Locations**: the list of saved locations, with details and a live
  forecast for the selected one.
  `a` search & add · `n` add by coordinates · `e` edit · `u` units ·
  `d` delete · `z` undo · `*` default · `J`/`K` reorder · `/` filter ·
  `Enter` show in Waybar · `s` save current
- **Current**: what Waybar shows right now, including the detected position.
  `g` toggle geolocation · `p` provider · `e` edit · `u` units ·
  `s` save as location
- **Settings**: every option, with a live bar preview.
  `←`/`→` change · `Enter` edit · `x` reset to default

Tabs: `1` `2` `3` / `Tab`. Help: `?`. The mouse works (click, double-click,
wheel). Every change is saved immediately, and a running daemon picks it up
right away. If the config changes outside the TUI, it reloads automatically.

## Config

`$XDG_CONFIG_HOME/wayweather/config.json` (normally
`~/.config/wayweather/config.json`) is created on first run. An existing
`config.toml` / `locations.json` from 2.x is imported automatically and left
untouched.

```jsonc
{
  "version": 3,
  "current": {                      // shown when geolocation is off
    "name": "", "city": "Vienna", "region": "Vienna", "country": "Austria",
    "latitude": 48.20849, "longitude": 16.37208,
    "units": {
      "temperature": "celsius",     // celsius | fahrenheit
      "wind_speed": "kmh",          // kmh | ms | mph | kn
      "precipitation": "mm",        // mm | inch
      "pressure": "hpa",            // hpa | inhg | mmhg | kpa
      "time_format": "24h"          // 24h | 12h
    }
  },
  "active": 0,                      // saved location loaded into "current"
  "geolocation": {
    "enabled": false,               // true: position overrides "current"
    "provider": "auto",             // auto | gpsd | geoclue | ip
    "gpsd_host": "localhost", "gpsd_port": 2947,
    "timeout": 5,                   // seconds per provider
    "cache_ttl": 600,               // reuse a fix for this long
    "reverse_geocode": true,        // name GPS fixes (OpenStreetMap)
    "include_in_cycle": true        // next/prev also visit geolocation
  },
  "display": {
    "icons": true,
    "text": "{temp}{tunit} {icon}", // bar text template
    "tooltip_title": "{place}",
    "tooltip": true,
    "hours": 6,                     // hourly rows in the tooltip (0-24)
    "days": 5,                      // daily rows in the tooltip (0-7)
    "decimals": 1                   // temperature decimals (0-2)
  },
  "refresh_interval": 900,          // daemon refresh, seconds
  "cache_ttl": 600,                 // weather cache for `get`, seconds
  "locations": [ { "name": "Work", "city": "Vienna", ..., "default": true } ]
}
```

Run `wayweather config edit` to edit the file in `$EDITOR`; it validates the
result afterwards.

**Template placeholders** (`wayweather help format`):
`{icon} {temp} {feels} {tunit} {desc} {condition} {tmin} {tmax} {humidity}
{dew} {precip} {punit} {precip_prob} {cloud} {wind} {gust} {wunit} {wind_dir}
{wind_arrow} {wind_deg} {pressure} {presunit} {uv} {visibility} {sunrise}
{sunset} {name} {city} {region} {country} {place} {lat} {lon} {time}
{updated} {date} {source}`

## Geolocation

`provider: "auto"` tries each source in turn and caches the fix for
`cache_ttl` seconds:

1. **gpsd**: a GPS receiver served by [gpsd](https://gpsd.io) on
   `localhost:2947`
2. **GeoClue2**: Wi-Fi, cell or GPS position via the desktop location
   service. Without a desktop agent (common on wlroots compositors), allow the
   app in `/etc/geoclue/geoclue.conf`:

   ```ini
   [wayweather]
   allowed=true
   system=false
   users=
   ```

3. **IP**: [ip-api.com](https://ip-api.com), with
   [ipwho.is](https://ipwho.is) as a fallback

Units always come from the current location. If geolocation fails, Waybar
falls back to the configured location.

## C library

`libwayweather` exposes the weather, place search and geolocation used by
the binary. It is installed with the binary (`/usr/include/wayweather.h`,
`libwayweather.so`, `libwayweather.a`, `wayweather.pc`) and only needs
libcurl, cJSON and (optionally) GeoClue, not ncurses. Only the `ww_*`
functions are exported; the library is thread-safe after `ww_init()` and
always uses `.` as the decimal separator, whatever your locale.

```c
#include <stdio.h>
#include <stdlib.h>
#include <wayweather.h>

int main(void) {
  ww_init();

  ww_location places[5];
  char err[256];
  int n = ww_geocode("Vienna", places, 5, err, sizeof err);
  if (n <= 0) {
    fprintf(stderr, "%s\n", err);
    return 1;
  }

  ww_weather w;
  if (ww_weather_for(&places[0], 600, &w, err, sizeof err) == 0) {
    char *text = ww_format("{icon} {temp}{tunit} {desc}", &places[0], &w, 1, 1);
    printf("%s, tomorrow %.0f°\n", text, w.days[1].temp_max);
    free(text);
  }

  /* Exactly what `wayweather get` prints, from the user's config */
  char *json = ww_waybar_json(NULL);
  puts(json);
  free(json);

  ww_cleanup();
  return 0;
}
```

```sh
cc demo.c $(pkg-config --cflags --libs wayweather) -o demo
```

The header documents every function: units and labels, `ww_geocode`,
`ww_reverse_geocode`, `ww_geolocate` (gpsd / GeoClue / IP),
`ww_current_location`, `ww_saved_locations`, `ww_weather_get`, WMO code
helpers, `ww_format` and `ww_waybar_json`.

---

Weather data by [Open-Meteo.com](https://open-meteo.com) (CC BY 4.0). Place
search by Open-Meteo Geocoding, reverse geocoding ©
[OpenStreetMap](https://www.openstreetmap.org/copyright) contributors.
