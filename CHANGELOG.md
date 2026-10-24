# CHANGELOG

## wayweather 3.0.1

- New: libwayweather, a C library with the public header `wayweather.h`
  (shared + static library and pkg-config file) for weather, place search,
  geolocation, formatting and Waybar JSON in other programs
- Fix: `wayweather get` exits 0 when it prints an error line, so Waybar no
  longer hides the module on network errors
- Fix: the daemon picks up config changes on the very first run (the
  config directory now exists before it is watched)
- Fix: a symlinked `config.json` (dotfile managers) stays a symlink; writes
  go to its target and the daemon watches the target
- Fix: `set --lat/--lon` no longer keeps the old city, region and country;
  `set --units` keeps the location's name and syncs its saved twin
- Fix: TUI hang when an error message didn't fit a very narrow pane
- Fix: wind direction is "-" instead of undefined behaviour when the API
  omits it; negative angles are handled
- Fix: cache files written from two threads can't collide
- The config lock file moved from `~/.config/wayweather` to
  `$XDG_RUNTIME_DIR/wayweather`
- TUI boxes have square corners
- Example Waybar CSS uses the theme's named (pywal) colours
- `make check` no longer needs Python and also tests the library
- Packaging: `VERSION` file, AUR files moved to `aur/wayweather`, the AUR
  package builds from the release tarball, GitHub Actions builds releases

## wayweather 3.0.0

Complete rewrite in C.

- New command line: `get`, `print`, `daemon`, `tui`, `search`, `add`, `list`,
  `remove`, `edit`, `use`, `default`, `next`, `prev`, `set`, `units`, `geo`,
  `refresh`, `reset`, `config`, `help` (the 2.x flags still work)
- New TUI: place search, add/edit/delete with undo, reordering,
  filtering, defaults, live forecast preview, settings editor, mouse support
- Per-location measurements: temperature, wind speed, precipitation,
  pressure and 12h/24h time
- Geolocation via gpsd, GeoClue2 or IP, with caching and named GPS fixes
- Daemon redraws instantly on config changes (inotify), refreshes on
  SIGUSR1 or `wayweather refresh`, and cycles on SIGUSR2 or `wayweather next`
- Offline support: cached forecasts, a `stale` class, retry with backoff
- Tooltip with hourly and daily forecast, sunrise/sunset, UV, pressure, dew
  point
- Templated bar text, CSS classes for condition, day/night and temperature
- Single JSON config (`config.json`); 2.x `config.toml`/`locations.json`
  are migrated automatically
- Makefile build (sources in `src/`, objects in `build/`, binary in `dist/`),
  smoke tests, man page, bash/zsh/fish completions
- Dependencies: libcurl, cJSON, ncursesw, optional libgeoclue
  (no more bash/jq/tombl)

## wayweather 2.1.1

- Fixed a self created issue

## wayweather 2.1.0

- Finally fix the icon issue
- Color code help and log messages

## wayweather 2.0.0

- Add Custom Locations, their loading, saving
- Add Default Location from Custom Locations
- Refactor much of the code
- Add Extra Help Arguments
- Add Default Location Setting
- Add Deleting Locations
- Add Daemon
- Add Version Checking

## wayweather 1.0.0

- Create script with basic functionality
- API parser
- Config reading and writing
