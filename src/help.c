#include "help.h"
#include "geolocate.h"
#include "util.h"
#include "version.h"

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <ncurses.h>
#include <string.h>

static bool color;

void help_set_color(bool c) { color = c; }

#define B (color ? "\033[1m" : "")
#define Y (color ? "\033[1;33m" : "")
#define G (color ? "\033[1;32m" : "")
#define C (color ? "\033[36m" : "")
#define D (color ? "\033[2m" : "")
#define R (color ? "\033[0m" : "")

typedef struct {
  const char *name;
  const char *args;
  const char *summary;
  const char *details;
} command_help;

static const command_help COMMANDS[] = {
    {"get", "[--no-icon] [-l LOC] [-f]",
     "Print Waybar JSON once (use with \"interval\")",
     "Options:\n"
     "  --no-icon          Don't include Nerd Font icons\n"
     "  -l, --location LOC Show a saved location instead of the current one\n"
     "  -f, --force        Bypass the weather and geolocation caches\n"},
    {"print", "[--no-icon] [-l LOC] [-f]",
     "Show a detailed report with forecast in the terminal",
     "Options:\n"
     "  --no-icon          Don't include Nerd Font icons\n"
     "  -l, --location LOC Show a saved location instead of the current one\n"
     "  -f, --force        Bypass the weather and geolocation caches\n"},
    {"daemon", "[--interval S] [--no-icon] [-l LOC]",
     "Stream Waybar JSON, updating on config changes",
     "Prints a new line whenever the weather or the config changes.\n"
     "Edits made with the TUI or other commands show up immediately.\n\n"
     "Options:\n"
     "  -i, --interval S   Refresh every S seconds (default: config)\n"
     "  --no-icon          Don't include Nerd Font icons\n"
     "  -l, --location LOC Always show this saved location\n\n"
     "Signals:\n"
     "  SIGUSR1  refresh now (also: wayweather refresh)\n"
     "  SIGUSR2  switch to the next location (also: wayweather next)\n"
     "  SIGHUP   reload the config\n"},
    {"tui", "", "Interactive manager for locations, units and settings",
     "Opens a full screen interface. Press ? inside for all key bindings.\n"
     "Running wayweather without a command in a terminal opens it too.\n"},
    {"search", "QUERY [-n N]", "Search places by name",
     "Narrow results with a comma: \"Springfield, Illinois\" or\n"
     "\"Paris, US\".\n\n"
     "Options:\n"
     "  -n, --count N      Number of results (default 10)\n"},
    {"add", "QUERY | --lat X --lon Y [OPTIONS]", "Save a new location",
     "Searches QUERY and saves the result. In a terminal you pick from\n"
     "the matches, otherwise the best match is used.\n\n"
     "Options:\n"
     "  -p, --pick N       Use the N-th search result\n"
     "  --lat X --lon Y    Coordinates (instead of a search)\n"
     "  --city, --region, --country TEXT   Place details\n"
     "  -N, --name TEXT    Label, e.g. \"Home\"\n"
     "  -u, --units P      metric or imperial\n"
     "  -d, --default      Make it the default location\n"
     "  --use              Show it right away\n"},
    {"list", "[--json]", "List saved locations",
     "  ★ marks the default location, ● the one currently shown.\n"},
    {"remove", "LOC... [-y]", "Delete saved locations",
     "LOC is a number from \"wayweather list\", a name or a city.\n\n"
     "Options:\n"
     "  -y, --yes          Don't ask for confirmation\n"},
    {"edit", "LOC [OPTIONS]", "Change a saved location",
     "Options:\n"
     "  -N, --name, --city, --region, --country TEXT\n"
     "  --lat X, --lon Y\n"
     "  -u, --units P      metric or imperial preset\n"
     "  --temp celsius|fahrenheit     --wind kmh|ms|mph|kn\n"
     "  --precip mm|inch              --pressure hpa|inhg|mmhg|kpa\n"
     "  --clock 24h|12h\n"},
    {"use", "LOC | geo", "Show a saved location (or geolocation)",
     "Loads a saved location as the current one and turns geolocation\n"
     "off. \"wayweather use geo\" switches back to geolocation.\n"},
    {"default", "[LOC | --clear]", "Show or set the default location",
     "The default location is used by \"wayweather use default\" and\n"
     "\"wayweather reset\".\n"},
    {"next", "", "Switch to the next saved location",
     "Great for Waybar's \"on-click\". Includes geolocation in the\n"
     "rotation unless geolocation.include_in_cycle is false.\n"},
    {"prev", "", "Switch to the previous saved location", ""},
    {"set", "--lat X --lon Y [OPTIONS]", "Set the current location manually",
     "Options:\n"
     "  --city, --region, --country, -N/--name TEXT\n"
     "  -u, --units P      metric or imperial\n"
     "  -s, --save         Also add it to the saved locations\n"
     "  --skip-config      With --save: only save, don't switch to it\n"},
    {"units", "[PRESET] [OPTIONS] [-l LOC]", "Show or change measurements",
     "Without arguments shows the units of the current location.\n\n"
     "  PRESET             metric or imperial\n"
     "  --temp celsius|fahrenheit     --wind kmh|ms|mph|kn\n"
     "  --precip mm|inch              --pressure hpa|inhg|mmhg|kpa\n"
     "  --clock 24h|12h\n"
     "  -l, --location LOC Change a saved location instead\n"},
    {"geo", "[on|off|toggle|locate|save [NAME]|provider P]",
     "Control GPS / IP geolocation",
     "  geo                Show status\n"
     "  geo on|off|toggle  Let geolocation override the current location\n"
     "  geo locate         Determine the position now and print it\n"
     "  geo save [NAME]    Save the detected position as a location\n"
     "  geo provider P     auto (gpsd, GeoClue, IP), gpsd, geoclue or ip\n"},
    {"refresh", "", "Make running daemons refetch now", ""},
    {"reset", "[--units] [--all] [-y]", "Reset the current location",
     "Clears the current location and turns geolocation back on.\n\n"
     "  -u, --units        Also reset units to metric\n"
     "  --all              Reset everything, including saved locations\n"
     "  -y, --yes          Don't ask for confirmation (with --all)\n"},
    {"config", "[path|show|edit|check]", "Inspect or edit the config file",
     ""},
    {"help", "[COMMAND|format]", "Show help for a command", ""},
};

static const char FORMAT_HELP[] =
    "Placeholders for display.text, display.tooltip_title and friends:\n\n"
    "  {icon} {temp} {feels} {tunit} {desc} {condition}\n"
    "  {tmin} {tmax} {humidity} {dew} {precip} {punit} {precip_prob}\n"
    "  {cloud} {wind} {gust} {wunit} {wind_dir} {wind_arrow} {wind_deg}\n"
    "  {pressure} {presunit} {uv} {visibility} {sunrise} {sunset}\n"
    "  {name} {city} {region} {country} {place} {lat} {lon}\n"
    "  {time} {updated} {date} {source}\n\n"
    "Use {{ and }} for literal braces. Example:\n"
    "  \"text\": \"{icon} {temp}{tunit} {wind_arrow}\"\n\n"
    "Waybar JSON also carries \"alt\" (condition) and CSS classes:\n"
    "  clear partly-cloudy cloudy fog drizzle rain freezing snow thunder\n"
    "  day night  hot warm mild cool cold  stale geolocated error\n";

void help_main(FILE *out) {
  fprintf(out,
          "%swayweather%s %s — weather for Waybar from Open-Meteo\n\n"
          "%sUsage:%s wayweather [GLOBAL OPTIONS] %sCOMMAND%s [ARGS]\n\n"
          "%sCommands:%s\n",
          B, R, WW_VERSION, G, R, Y, R, G, R);
  for (size_t i = 0; i < ARRAY_LEN(COMMANDS); i++) {
    int pad = 10 - (int)strlen(COMMANDS[i].name);
    fprintf(out, "  %s%s%s%*s%s\n", Y, COMMANDS[i].name, R, pad, "",
            COMMANDS[i].summary);
  }
  fprintf(out,
          "\n%sGlobal options:%s\n"
          "  -c, --config FILE  Use another config file\n"
          "  --verbose          Log what's happening (twice for debug)\n"
          "  --no-color         Disable colors\n"
          "  -h, --help         Show help (also: COMMAND --help)\n"
          "  -v, --version      Show version\n\n"
          "%sLOC%s is a number from \"wayweather list\", a name, a city or "
          "\"default\".\n"
          "Without a command, wayweather opens the TUI in a terminal and "
          "prints\nWaybar JSON otherwise.\n\n"
          "%sMore:%s wayweather help format · man wayweather · %s%s%s\n",
          G, R, C, R, G, R, C, WW_URL, R);
}

bool help_command(FILE *out, const char *name) {
  if (strcmp(name, "format") == 0 || strcmp(name, "formats") == 0) {
    fputs(FORMAT_HELP, out);
    return true;
  }
  for (size_t i = 0; i < ARRAY_LEN(COMMANDS); i++) {
    const command_help *c = &COMMANDS[i];
    if (strcmp(c->name, name) != 0)
      continue;
    fprintf(out, "%sUsage:%s wayweather %s%s%s %s\n\n%s\n", G, R, Y, c->name,
            R, c->args, c->summary);
    if (c->details[0])
      fprintf(out, "\n%s", c->details);
    return true;
  }
  return false;
}

void help_version(FILE *out) {
  fprintf(out, "wayweather %s\n", WW_VERSION);
  fprintf(out, "%slibcurl %s, cJSON %s, %s, GeoClue: %s%s\n", D,
          curl_version_info(CURLVERSION_NOW)->version, cJSON_Version(),
          curses_version(), geolocate_has_geoclue() ? "yes" : "no", R);
  fprintf(out, "Weather data by Open-Meteo.com (CC BY 4.0)\n");
}
