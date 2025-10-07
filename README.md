# WayWeather

Custom Weather Script for Waybar with IP Geolocation,
Custom Locations, Default Locations and many more things.

It got out of the original scope the more I worked on it xd.

[![AUR Version](https://img.shields.io/aur/version/wayweather?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/wayweather)

## Sections

- [**USAGE**](#usage)
- [INSTALLATION](#installation)
- [WAYBAR MODULE](#waybar-module)
- [CONFIG](#config)

## Usage

```
> wayweather [-h/--help] [OPTIONS]

Weather script for Waybar with IP Geolocation

HELP:
    -h, --help            Print main help information
    <OPTION> -h, --help   Print help information about
                          an option
    Example: > wayweather --get --help

OPTIONS:
    -g, --get [--no-icon]   Pring waybar json input
    -s, --set <ARGS>        Set custom location and
                            print waybar json input
    -r, --reset [--units]   Reset custom location to
                            IP geolocation
    -p, --print [--no-icon] Print waybar result to stdout
    -l, --load [default]    Select locations from saved ones
        --list              List saved locations
    -sd, --set-default      Set default location for faster
                            loading
    -d, --delete   [WIP]    Delete locations from saved ones
    -w, --daemon   [WIP]    Runs a while loop for waybar
                            for faster updating without
                            polling the API
```

### For more information, go over to the [Wiki](https://github.com/TheElevatedOne/wayweather/wiki)

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

The saved locations are at `$HOME/.config/wayweather/locations.json`
and will be created when saving locations.

```json
{
  "0": {
    "latitude": 40.71,
    "longitude": -74.01,
    "city": "New York",
    "country": "USA",
    "units": "imp",
    "default": false
  },
  "1": {
  ...
  },
  ...
}
```

- has a similar structure to the config but supports saviing multiple locations
- also supports setting a default location
