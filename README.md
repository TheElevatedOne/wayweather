# wayweather

Custom Weather Script for Waybar with IP Geolocation

[![AUR Version](https://img.shields.io/aur/version/wayweather?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/wayweather)

## Sections

- [**USAGE**](#usage)
- [INSTALLATION](#installation)
- [WAYBAR MODULE](#waybar-module)
- [CONFIG](#config)

## Usage

```
> wayweather [-h/--help] [--get] [--set] [--reset] [--print]

Weather script for Waybar with IP Geolocation

OPTIONS:
    -h, --help      Print help information
    --get           Pring waybar json input
    --set <ARGS>    Set custom location and
                    print waybar json input
    --reset         Reset custom location to
                    IP geolocation
    --print         Print waybar result to stdout

ARGS:
  Arguments for setting custom location
  Enclose in double quotes (") for expected result

  INFO: Aruments --lat, --long, --city, --country must
  be used together, --units may be used separately

    --lat=LATITUDE      Latitude in decimal format
                        with two decimal places
    --long=LONGITUDE    Longitude in decimal format
                        with two decimal places
    --city=CITY         City name
    --country=COUNTRY   Country name
    --units=UNITS       Either "met" or "imp" for
                        metric and imperial units

EXAMPLES:
  
  > wayweather --set --lat="40.71" --long="-74.01" --city="New York" --country="USA"
  
  > wayweather --get
  {"text":"71.1°F <big>󰖔 </big>","tooltip":"New York, USA\nTime: 2025-09-30 19:45\n\n
  Temperature: 71.1°F\nHumidity: 53%\nPrecipitaion: 0.00 inch\nCloud Cover: 19%\n
  Wind Direct: North West (356°)\nWind Speed: 6.3 mph\nWind Gusts: 15.2 mph"}
```

## Installation

**Required Packages:**

- curl
- awk
- jq
- tombl
- font from [nerdfonts](https://www.nerdfonts.com/font-downloads) or a [patched font](https://github.com/ryanoasis/nerd-fonts?tab=readme-ov-file#font-patcher)

1. **Cloning**
    - Clone the repo via `git clone https://github.com/TheElevatedOne/wayweather.git`
    - Use the script in that directory
2. **AUR**
    - [wayweather](https://aur.archlinux.org/packages/wayweather)
    - `yay -S wayweather`, `paru -S wayweather`, etc.

## Waybar Module

```json
  "custom/wayweather": {
    "format": "{text}",
    "interval": 900,
    "exec": "path/to/wayweather --get",
    "return-type": "json"
  },
```

## Config

The configuration file is at `$HOME/.config/wayweather/config.toml`
and will be created on first run.

```toml
# WayWeather config
# ip_loc enables/disables autolocation through ip address
# latitide, longitude, city and country must be set when ip_loc is false
# units can be either "imp" or "met" (imperial and metric)

[wayweather]
ip_loc = false
latitude = 40.71
longitude = -74.01
city = "New York"
country = "USA"
units = "met"

```

- ip_loc - can be `true` or `false`, when setting location with `--set` it auto-disables itself
- latitide & longitude - numbers in decimal coordinate format rounded to two digits
  - get them from [https://www.gps-coordinates.net/](https://www.gps-coordinates.net/) (ex.)
- city - city name
- country - country name
- units - either `met` or `imp` for metric and imperial
  - `met` - °C, mm, km/h
  - `imp` - °F, inch, mph
