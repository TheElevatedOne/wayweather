# CHANGELOG

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
