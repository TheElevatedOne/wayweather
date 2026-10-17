#define _GNU_SOURCE
#include "app.h"
#include "config.h"
#include "daemon.h"
#include "geocode.h"
#include "geolocate.h"
#include "help.h"
#include "http.h"
#include "log.h"
#include "render.h"
#include "tui.h"
#include "units.h"
#include "util.h"
#include "version.h"

#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool use_color;

/* ---------- Small helpers ---------- */

static bool interactive(void) {
  return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

static bool confirm(const char *question) {
  if (!interactive())
    return false;
  printf("%s [y/N] ", question);
  fflush(stdout);
  char line[16];
  if (!fgets(line, sizeof line, stdin))
    return false;
  return line[0] == 'y' || line[0] == 'Y';
}

static int usage_error(const char *cmd, const char *msg) {
  log_error("%s", msg);
  fprintf(stderr, "Try \"wayweather help %s\".\n", cmd);
  return 2;
}

/* Load + lock for read-modify-write commands */
static bool begin_edit(config_t *cfg, int *lock) {
  *lock = config_lock();
  if (!config_load(cfg)) {
    config_unlock(*lock);
    return false;
  }
  return true;
}

static int end_edit(config_t *cfg, int lock, bool save) {
  bool ok = !save || config_save(cfg);
  config_free(cfg);
  config_unlock(lock);
  return ok ? 0 : 1;
}

static int find_or_complain(const config_t *cfg, const char *ref) {
  int idx = config_find(cfg, ref);
  if (idx == -2)
    log_error("\"%s\" matches several saved locations, use its number "
              "from \"wayweather list\"",
              ref);
  else if (idx < 0)
    log_error("No saved location matches \"%s\" (see \"wayweather list\")",
              ref);
  return idx;
}

static void describe(const location_t *loc, char *buf, size_t size) {
  char label[FIELD_LEN], place[512];
  location_label(loc, label, sizeof label);
  location_place(loc, place, sizeof place);
  if (strcmp(label, loc->city) == 0 || str_empty(loc->city))
    snprintf(buf, size, "%s", place);
  else
    snprintf(buf, size, "%s (%s)", label, place);
}

/* Parses unit options shared by `units` and `edit`; true if handled */
static bool unit_option(int opt, const char *arg, units_t *u, bool *bad) {
  int v;
  switch (opt) {
  case 'u':
    if (!units_from_preset(arg, u))
      *bad = true;
    return true;
  case 1001:
    if ((v = unit_lookup(TEMP_IDS, TEMP_COUNT, arg)) < 0)
      *bad = true;
    else
      u->temperature = v;
    return true;
  case 1002:
    if ((v = unit_lookup(WIND_IDS, WIND_COUNT, arg)) < 0)
      *bad = true;
    else
      u->wind = v;
    return true;
  case 1003:
    if ((v = unit_lookup(PRECIP_IDS, PRECIP_COUNT, arg)) < 0)
      *bad = true;
    else
      u->precipitation = v;
    return true;
  case 1004:
    if ((v = unit_lookup(PRESSURE_IDS, PRESSURE_COUNT, arg)) < 0)
      *bad = true;
    else
      u->pressure = v;
    return true;
  case 1005:
    if ((v = unit_lookup(CLOCK_IDS, CLOCK_COUNT, arg)) < 0)
      *bad = true;
    else
      u->clock = v;
    return true;
  }
  return false;
}

#define UNIT_LONGOPTS                                                          \
  {"units", required_argument, 0, 'u'},                                        \
      {"temp", required_argument, 0, 1001},                                    \
      {"temperature", required_argument, 0, 1001},                             \
      {"wind", required_argument, 0, 1002},                                    \
      {"precip", required_argument, 0, 1003},                                  \
      {"precipitation", required_argument, 0, 1003},                           \
      {"pressure", required_argument, 0, 1004},                                \
      {"clock", required_argument, 0, 1005}

/* Location detail options shared by add / edit / set; true if handled */
static bool place_option(int opt, const char *arg, location_t *loc,
                         bool *coords, bool *bad) {
  switch (opt) {
  case 'N':
    str_copy(loc->name, arg, sizeof loc->name);
    return true;
  case 2001:
    str_copy(loc->city, arg, sizeof loc->city);
    return true;
  case 2002:
    str_copy(loc->region, arg, sizeof loc->region);
    return true;
  case 2003:
    str_copy(loc->country, arg, sizeof loc->country);
    return true;
  case 2004:
    if (!parse_double(arg, &loc->latitude))
      *bad = true;
    *coords = true;
    return true;
  case 2005:
    if (!parse_double(arg, &loc->longitude))
      *bad = true;
    *coords = true;
    return true;
  }
  return false;
}

#define PLACE_LONGOPTS                                                         \
  {"name", required_argument, 0, 'N'}, {"city", required_argument, 0, 2001},  \
      {"region", required_argument, 0, 2002},                                  \
      {"country", required_argument, 0, 2003},                                 \
      {"lat", required_argument, 0, 2004},                                     \
      {"latitude", required_argument, 0, 2004},                                \
      {"lon", required_argument, 0, 2005},                                     \
      {"long", required_argument, 0, 2005},                                    \
      {"longitude", required_argument, 0, 2005}

/* ---------- get / print ---------- */

static int cmd_show(int argc, char **argv, bool waybar) {
  static const struct option opts[] = {
      {"no-icon", no_argument, 0, 'I'},
      {"location", required_argument, 0, 'l'},
      {"force", no_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  bool no_icon = false, force = false;
  const char *ref = NULL;
  int c;
  while ((c = getopt_long(argc, argv, "l:fh", opts, NULL)) != -1) {
    switch (c) {
    case 'I':
      no_icon = true;
      break;
    case 'l':
      ref = optarg;
      break;
    case 'f':
      force = true;
      break;
    case 'h':
      help_command(stdout, argv[0]);
      return 0;
    default:
      return usage_error(argv[0], "Invalid option");
    }
  }

  config_t cfg;
  if (!config_load(&cfg)) {
    if (waybar) {
      char *line = render_waybar_error("?", "Config file is invalid");
      puts(line);
      free(line);
    }
    return 1;
  }
  if (no_icon)
    cfg.display.icons = false;

  location_t loc;
  weather_t w;
  char source[96], err[512];
  int status = 0;
  if (app_fetch(&cfg, ref, force, cfg.cache_ttl, &loc, &w, source,
                sizeof source, err, sizeof err)) {
    render_ctx ctx = {&cfg, &loc, &w, source, cfg.display.icons};
    if (waybar) {
      char *line = render_waybar(&ctx);
      puts(line);
      free(line);
    } else {
      render_print(stdout, &ctx, use_color);
    }
  } else {
    log_error("%s", err);
    if (waybar) {
      char *line = render_waybar_error(
          cfg.display.icons ? "\U000F0F2F" : "?", err);
      puts(line);
      free(line);
    }
    status = 1;
  }
  config_free(&cfg);
  return status;
}

/* ---------- daemon ---------- */

static int cmd_daemon(int argc, char **argv) {
  static const struct option opts[] = {
      {"interval", required_argument, 0, 'i'},
      {"timer", required_argument, 0, 'i'}, /* legacy */
      {"no-icon", no_argument, 0, 'I'},
      {"location", required_argument, 0, 'l'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  daemon_opts d = {0};
  long v;
  int c;
  while ((c = getopt_long(argc, argv, "i:l:h", opts, NULL)) != -1) {
    switch (c) {
    case 'i':
      if (!parse_long(optarg, &v) || v < 30)
        return usage_error("daemon", "Interval must be at least 30 seconds");
      d.interval = (int)v;
      break;
    case 'I':
      d.no_icon = true;
      break;
    case 'l':
      d.location = optarg;
      break;
    case 'h':
      help_command(stdout, "daemon");
      return 0;
    default:
      return usage_error("daemon", "Invalid option");
    }
  }
  return daemon_run(&d);
}

/* ---------- search / add ---------- */

static void print_result(int n, const geocode_result *r) {
  char place[512];
  location_place(&r->loc, place, sizeof place);
  printf("%s%3d.%s %s %s(%.4f, %.4f", use_color ? "\033[1;33m" : "", n,
         use_color ? "\033[0m" : "", place, use_color ? "\033[2m" : "",
         r->loc.latitude, r->loc.longitude);
  if (r->population >= 1000000)
    printf(", pop. %.1fM", r->population / 1e6);
  else if (r->population >= 1000)
    printf(", pop. %ldk", r->population / 1000);
  printf(")%s\n", use_color ? "\033[0m" : "");
}

static char *join_args(int argc, char **argv) {
  strbuf sb;
  sb_init(&sb);
  for (int i = 0; i < argc; i++) {
    if (i)
      sb_appendc(&sb, ' ');
    sb_append(&sb, argv[i]);
  }
  return sb_steal(&sb);
}

static int cmd_search(int argc, char **argv) {
  static const struct option opts[] = {{"count", required_argument, 0, 'n'},
                                       {"help", no_argument, 0, 'h'},
                                       {0, 0, 0, 0}};
  long count = 10;
  int c;
  while ((c = getopt_long(argc, argv, "n:h", opts, NULL)) != -1) {
    switch (c) {
    case 'n':
      if (!parse_long(optarg, &count) || count < 1 || count > 50)
        return usage_error("search", "Count must be between 1 and 50");
      break;
    case 'h':
      help_command(stdout, "search");
      return 0;
    default:
      return usage_error("search", "Invalid option");
    }
  }
  if (optind >= argc)
    return usage_error("search", "What should I search for?");

  char *query = join_args(argc - optind, argv + optind);
  geocode_result *res = xcalloc((size_t)count, sizeof *res);
  char err[256];
  int n = geocode_search(query, (int)count, res, err, sizeof err);
  free(query);
  if (n <= 0) {
    log_error("%s", err);
    free(res);
    return 1;
  }
  for (int i = 0; i < n; i++)
    print_result(i + 1, &res[i]);
  printf("\nSave one with: wayweather add \"<name>\" --pick N\n");
  free(res);
  return 0;
}

static int cmd_add(int argc, char **argv) {
  static const struct option opts[] = {
      PLACE_LONGOPTS,
      {"units", required_argument, 0, 'u'},
      {"pick", required_argument, 0, 'p'},
      {"default", no_argument, 0, 'd'},
      {"use", no_argument, 0, 'U'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  location_t loc;
  location_init(&loc);
  location_t over; /* user overrides applied on top of a search result */
  memset(&over, 0, sizeof over);
  bool coords = false, bad = false, make_default = false, use = false;
  bool units_set = false;
  long pick = 0;
  int c;
  while ((c = getopt_long(argc, argv, "N:u:p:dh", opts, NULL)) != -1) {
    if (place_option(c, optarg, &over, &coords, &bad))
      continue;
    switch (c) {
    case 'u':
      if (!units_from_preset(optarg, &loc.units))
        return usage_error("add", "Units must be metric or imperial");
      units_set = true;
      break;
    case 'p':
      if (!parse_long(optarg, &pick) || pick < 1)
        return usage_error("add", "--pick expects a positive number");
      break;
    case 'd':
      make_default = true;
      break;
    case 'U':
      use = true;
      break;
    case 'h':
      help_command(stdout, "add");
      return 0;
    default:
      return usage_error("add", "Invalid option");
    }
  }
  if (bad)
    return usage_error("add", "Coordinates must be numbers");

  if (coords) {
    units_t u = loc.units;
    loc = over;
    loc.units = u;
  } else {
    if (optind >= argc)
      return usage_error("add", "Give a place to search for or --lat/--lon");
    char *query = join_args(argc - optind, argv + optind);
    geocode_result res[10];
    char err[256];
    int n = geocode_search(query, 10, res, err, sizeof err);
    free(query);
    if (n <= 0) {
      log_error("%s", err);
      return 1;
    }
    int choice = 0;
    if (pick) {
      if (pick > n) {
        log_error("Only %d result(s) found", n);
        return 1;
      }
      choice = (int)pick - 1;
    } else if (n > 1 && interactive()) {
      for (int i = 0; i < n; i++)
        print_result(i + 1, &res[i]);
      printf("Which one? [1-%d, Enter = 1] ", n);
      fflush(stdout);
      char line[16];
      long v;
      if (fgets(line, sizeof line, stdin) && parse_long(str_trim(line), &v)) {
        if (v < 1 || v > n) {
          log_error("Invalid choice");
          return 1;
        }
        choice = (int)v - 1;
      }
    }
    units_t u = loc.units;
    loc = res[choice].loc;
    if (units_set)
      loc.units = u;
    if (!str_empty(over.name))
      str_copy(loc.name, over.name, sizeof loc.name);
    if (!str_empty(over.city))
      str_copy(loc.city, over.city, sizeof loc.city);
    if (!str_empty(over.region))
      str_copy(loc.region, over.region, sizeof loc.region);
    if (!str_empty(over.country))
      str_copy(loc.country, over.country, sizeof loc.country);
  }

  char err[128];
  if (!location_validate(&loc, err, sizeof err)) {
    log_error("%s", err);
    return 1;
  }
  loc.is_default = make_default;

  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  for (int i = 0; i < cfg.n_locations; i++) {
    if (location_same_place(&cfg.locations[i], &loc)) {
      char desc[256];
      describe(&cfg.locations[i], desc, sizeof desc);
      log_error("Already saved as #%d: %s", i + 1, desc);
      return end_edit(&cfg, lock, false) | 1;
    }
  }
  int idx = config_add(&cfg, &loc);
  if (idx < 0) {
    log_error("Too many saved locations (max %d)", MAX_LOCATIONS);
    return end_edit(&cfg, lock, false) | 1;
  }
  if (use)
    config_use(&cfg, idx);
  char desc[256], units[64];
  describe(&loc, desc, sizeof desc);
  units_describe(&loc.units, units, sizeof units);
  printf("Saved #%d: %s [%s]%s%s\n", idx + 1, desc, units,
         make_default ? " ★ default" : "", use ? " — now showing" : "");
  return end_edit(&cfg, lock, true);
}

/* ---------- list / remove / edit ---------- */

static int cmd_list(int argc, char **argv) {
  bool json = argc > 1 && strcmp(argv[1], "--json") == 0;
  if (argc > 1 && !json) {
    help_command(stdout, "list");
    return strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ? 0
                                                                         : 2;
  }
  config_t cfg;
  if (!config_load(&cfg))
    return 1;
  if (json) {
    char *s = config_locations_json(&cfg);
    puts(s);
    free(s);
    config_free(&cfg);
    return 0;
  }

  if (cfg.n_locations == 0) {
    printf("No saved locations yet. Add one with \"wayweather add <city>\" "
           "or the TUI.\n");
  }
  int active = cfg.geo.enabled ? -1 : config_match_current(&cfg);
  const char *dim = use_color ? "\033[2m" : "";
  const char *bold = use_color ? "\033[1m" : "";
  const char *yellow = use_color ? "\033[1;33m" : "";
  const char *green = use_color ? "\033[1;32m" : "";
  const char *reset = use_color ? "\033[0m" : "";
  for (int i = 0; i < cfg.n_locations; i++) {
    const location_t *l = &cfg.locations[i];
    char label[FIELD_LEN], place[512], units[64];
    location_label(l, label, sizeof label);
    location_place(l, place, sizeof place);
    units_describe(&l->units, units, sizeof units);
    strbuf line;
    sb_init(&line);
    sb_printf(&line, "%s%3d%s %s%s%s%s%s ", yellow, i + 1, reset, yellow,
              l->is_default ? "★" : " ", green, i == active ? "●" : " ",
              reset);
    sb_append(&line, bold);
    sb_append_padded(&line, label, 18);
    sb_append(&line, reset);
    sb_printf(&line, " %s  %s(%.4f, %.4f · %s)%s", place, dim, l->latitude,
              l->longitude, units, reset);
    puts(line.data);
    sb_free(&line);
  }
  if (cfg.geo.enabled)
    printf("%s    Currently showing: geolocation (%s)%s\n", dim,
           GEO_PROVIDER_IDS[cfg.geo.provider], reset);
  else if (active < 0 && app_location_set(&cfg.current)) {
    char desc[256];
    describe(&cfg.current, desc, sizeof desc);
    printf("%s    Currently showing: %s (not saved)%s\n", dim, desc, reset);
  }
  config_free(&cfg);
  return 0;
}

static int cmp_desc(const void *a, const void *b) {
  return *(const int *)b - *(const int *)a;
}

static int cmd_remove(int argc, char **argv) {
  static const struct option opts[] = {{"yes", no_argument, 0, 'y'},
                                       {"help", no_argument, 0, 'h'},
                                       {0, 0, 0, 0}};
  bool yes = false;
  int c;
  while ((c = getopt_long(argc, argv, "yh", opts, NULL)) != -1) {
    switch (c) {
    case 'y':
      yes = true;
      break;
    case 'h':
      help_command(stdout, "remove");
      return 0;
    default:
      return usage_error("remove", "Invalid option");
    }
  }
  if (optind >= argc)
    return usage_error("remove", "Which location? (see \"wayweather list\")");

  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  int n = argc - optind;
  int *idx = xcalloc((size_t)n, sizeof *idx);
  for (int i = 0; i < n; i++) {
    idx[i] = find_or_complain(&cfg, argv[optind + i]);
    if (idx[i] < 0) {
      free(idx);
      return end_edit(&cfg, lock, false) | 1;
    }
  }
  qsort(idx, (size_t)n, sizeof *idx, cmp_desc);

  if (!yes) {
    if (!interactive()) {
      log_error("Refusing to delete without confirmation, pass --yes");
      free(idx);
      return end_edit(&cfg, lock, false) | 1;
    }
    for (int i = 0; i < n; i++) {
      char desc[256];
      describe(&cfg.locations[idx[i]], desc, sizeof desc);
      printf("  #%d %s\n", idx[i] + 1, desc);
    }
    if (!confirm(n == 1 ? "Delete this location?" : "Delete these locations?")) {
      printf("Nothing deleted.\n");
      free(idx);
      return end_edit(&cfg, lock, false);
    }
  }
  for (int i = 0; i < n; i++) {
    if (i > 0 && idx[i] == idx[i - 1])
      continue;
    char desc[256];
    describe(&cfg.locations[idx[i]], desc, sizeof desc);
    config_remove(&cfg, idx[i]);
    printf("Deleted %s\n", desc);
  }
  free(idx);
  return end_edit(&cfg, lock, true);
}

/* Options collected first so presets can be applied before overrides */
typedef struct {
  int opt;
  const char *arg;
} parsed_opt;

#define MAX_OPTS 64

/* Runs getopt over all options; returns the count or -1 on an invalid one.
 * GNU getopt leaves positional arguments at argv[optind..argc). */
static int collect_opts(int argc, char **argv, const char *shortopts,
                        const struct option *longopts, parsed_opt *out) {
  int n = 0, c;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == '?' || n >= MAX_OPTS)
      return -1;
    out[n++] = (parsed_opt){c, optarg};
  }
  return n;
}

static bool has_opt(const parsed_opt *opts, int n, int opt) {
  for (int i = 0; i < n; i++)
    if (opts[i].opt == opt)
      return true;
  return false;
}

/* Applies unit presets (-u) first, then the individual unit options */
static bool apply_units(const parsed_opt *opts, int n, units_t *u) {
  bool bad = false;
  for (int i = 0; i < n; i++)
    if (opts[i].opt == 'u')
      unit_option('u', opts[i].arg, u, &bad);
  for (int i = 0; i < n; i++)
    if (opts[i].opt != 'u')
      unit_option(opts[i].opt, opts[i].arg, u, &bad);
  return !bad;
}

static int cmd_edit(int argc, char **argv) {
  static const struct option longopts[] = {PLACE_LONGOPTS, UNIT_LONGOPTS,
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};
  parsed_opt opts[MAX_OPTS];
  int n = collect_opts(argc, argv, "N:u:h", longopts, opts);
  if (n < 0)
    return usage_error("edit", "Invalid option");
  if (has_opt(opts, n, 'h')) {
    help_command(stdout, "edit");
    return 0;
  }
  if (optind >= argc)
    return usage_error("edit", "Which location? (see \"wayweather list\")");
  if (n == 0)
    return usage_error("edit", "Nothing to change. Tip: \"wayweather tui\" "
                               "edits interactively");

  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  int idx = find_or_complain(&cfg, argv[optind]);
  if (idx < 0)
    return end_edit(&cfg, lock, false) | 1;

  location_t loc = cfg.locations[idx];
  bool coords = false, bad = !apply_units(opts, n, &loc.units);
  for (int i = 0; i < n; i++)
    place_option(opts[i].opt, opts[i].arg, &loc, &coords, &bad);
  if (bad) {
    end_edit(&cfg, lock, false);
    return usage_error("edit", "Invalid value (see \"wayweather help edit\")");
  }
  char err[128];
  if (!location_validate(&loc, err, sizeof err)) {
    log_error("%s", err);
    return end_edit(&cfg, lock, false) | 1;
  }
  bool was_active = !cfg.geo.enabled && config_match_current(&cfg) == idx;
  cfg.locations[idx] = loc;
  if (was_active)
    config_use(&cfg, idx); /* keep the shown location in sync */
  char desc[256], units[64];
  describe(&loc, desc, sizeof desc);
  units_describe(&loc.units, units, sizeof units);
  printf("Updated #%d: %s [%s]\n", idx + 1, desc, units);
  return end_edit(&cfg, lock, true);
}

/* ---------- use / default / next / prev ---------- */

static int cmd_use(int argc, char **argv) {
  if (argc < 2)
    return usage_error("use", "Which location? (see \"wayweather list\")");
  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    help_command(stdout, "use");
    return 0;
  }
  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  if (str_ieq(argv[1], "geo") || str_ieq(argv[1], "geolocation") ||
      str_ieq(argv[1], "auto")) {
    cfg.geo.enabled = true;
    cfg.active = -1;
    printf("Now showing: geolocation (%s)\n",
           GEO_PROVIDER_IDS[cfg.geo.provider]);
    return end_edit(&cfg, lock, true);
  }
  int idx = find_or_complain(&cfg, argv[1]);
  if (idx < 0) {
    if (str_ieq(argv[1], "default"))
      log_error("No default location set (\"wayweather default LOC\")");
    return end_edit(&cfg, lock, false) | 1;
  }
  config_use(&cfg, idx);
  char desc[256];
  describe(&cfg.current, desc, sizeof desc);
  printf("Now showing: %s\n", desc);
  return end_edit(&cfg, lock, true);
}

static int cmd_default(int argc, char **argv) {
  config_t cfg;
  int lock;
  if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    help_command(stdout, "default");
    return 0;
  }
  if (!begin_edit(&cfg, &lock))
    return 1;
  if (argc < 2) {
    int def = config_default_index(&cfg);
    if (def < 0) {
      printf("No default location set.\n");
    } else {
      char desc[256];
      describe(&cfg.locations[def], desc, sizeof desc);
      printf("Default: #%d %s\n", def + 1, desc);
    }
    return end_edit(&cfg, lock, false);
  }
  if (strcmp(argv[1], "--clear") == 0 || strcmp(argv[1], "none") == 0) {
    config_set_default(&cfg, -1);
    printf("Default location cleared.\n");
    return end_edit(&cfg, lock, true);
  }
  int idx = find_or_complain(&cfg, argv[1]);
  if (idx < 0)
    return end_edit(&cfg, lock, false) | 1;
  config_set_default(&cfg, idx);
  char desc[256];
  describe(&cfg.locations[idx], desc, sizeof desc);
  printf("Default: #%d %s\n", idx + 1, desc);
  return end_edit(&cfg, lock, true);
}

static int cmd_cycle(int direction) {
  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  char desc[128];
  bool ok = app_cycle(&cfg, direction, desc, sizeof desc);
  if (ok)
    printf("Now showing: %s\n", desc);
  else
    log_error("%s", desc);
  return end_edit(&cfg, lock, ok) | (ok ? 0 : 1);
}

/* ---------- set / units ---------- */

static int cmd_set(int argc, char **argv) {
  static const struct option opts[] = {
      PLACE_LONGOPTS,
      {"units", required_argument, 0, 'u'},
      {"save", no_argument, 0, 's'},
      {"skip-config", no_argument, 0, 'S'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  location_t loc = cfg.current;
  loc.name[0] = '\0';
  bool coords = false, bad = false, save = false, skip = false;
  bool units_only = true;
  int c;
  while ((c = getopt_long(argc, argv, "N:u:sh", opts, NULL)) != -1) {
    if (place_option(c, optarg, &loc, &coords, &bad)) {
      units_only = false;
      continue;
    }
    switch (c) {
    case 'u':
      if (!units_from_preset(optarg, &loc.units))
        bad = true;
      break;
    case 's':
      save = true;
      break;
    case 'S':
      skip = true;
      break;
    case 'h':
      help_command(stdout, "set");
      return end_edit(&cfg, lock, false);
    default:
      end_edit(&cfg, lock, false);
      return usage_error("set", "Invalid option");
    }
  }
  if (bad) {
    end_edit(&cfg, lock, false);
    return usage_error("set", "Invalid value");
  }
  if (argc < 2) {
    end_edit(&cfg, lock, false);
    return usage_error("set", "Nothing to set");
  }
  if (!units_only && !coords) {
    end_edit(&cfg, lock, false);
    return usage_error("set", "A new location needs --lat and --lon "
                              "(or use \"wayweather add <city> --use\")");
  }
  char err[128];
  if (!units_only && !location_validate(&loc, err, sizeof err)) {
    log_error("%s", err);
    return end_edit(&cfg, lock, false) | 1;
  }

  if (save) {
    loc.is_default = false;
    int idx = config_add(&cfg, &loc);
    if (idx >= 0)
      printf("Saved as #%d\n", idx + 1);
  }
  if (!skip) {
    cfg.current = loc;
    if (!units_only) {
      cfg.geo.enabled = false;
      cfg.active = config_match_current(&cfg);
    }
    char desc[256], units[64];
    describe(&loc, desc, sizeof desc);
    units_describe(&loc.units, units, sizeof units);
    printf("Current location: %s [%s]\n", desc, units);
  }
  return end_edit(&cfg, lock, true);
}

static int cmd_units(int argc, char **argv) {
  static const struct option longopts[] = {
      UNIT_LONGOPTS,
      {"location", required_argument, 0, 'l'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  parsed_opt opts[MAX_OPTS];
  int n = collect_opts(argc, argv, "u:l:h", longopts, opts);
  if (n < 0)
    return usage_error("units", "Invalid option");
  if (has_opt(opts, n, 'h')) {
    help_command(stdout, "units");
    return 0;
  }
  const char *ref = NULL;
  for (int i = 0; i < n; i++)
    if (opts[i].opt == 'l')
      ref = opts[i].arg;

  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  int idx = -1;
  if (ref && (idx = find_or_complain(&cfg, ref)) < 0)
    return end_edit(&cfg, lock, false) | 1;
  units_t *u = idx >= 0 ? &cfg.locations[idx].units : &cfg.current.units;

  /* Positional presets first, then -u presets, then single units */
  bool bad = false;
  for (int i = optind; i < argc; i++)
    if (!units_from_preset(argv[i], u))
      bad = true;
  if (!apply_units(opts, n, u))
    bad = true;
  bool changed = optind < argc || n > (ref ? 1 : 0);
  if (bad) {
    end_edit(&cfg, lock, false);
    return usage_error("units", "Invalid unit (see \"wayweather help units\")");
  }

  /* Keep current and its saved twin consistent */
  int match = cfg.geo.enabled ? -1 : config_match_current(&cfg);
  if (changed && match >= 0) {
    if (idx < 0)
      cfg.locations[match].units = cfg.current.units;
    else if (idx == match)
      cfg.current.units = cfg.locations[idx].units;
  }

  char desc[64];
  units_describe(u, desc, sizeof desc);
  printf("%s%s: %s (%s)\n", changed ? "Units set for " : "Units of ",
         idx >= 0 ? "saved location" : "current location", desc,
         units_preset_name(u));
  return end_edit(&cfg, lock, changed);
}

/* ---------- geo ---------- */

static int cmd_geo(int argc, char **argv) {
  const char *sub = argc > 1 ? argv[1] : "status";
  if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0) {
    help_command(stdout, "geo");
    return 0;
  }
  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;

  if (strcmp(sub, "status") == 0) {
    printf("Geolocation: %s\nProvider:    %s%s\n",
           cfg.geo.enabled ? "on (overrides the current location)" : "off",
           GEO_PROVIDER_IDS[cfg.geo.provider],
           geolocate_has_geoclue() ? "" : " (built without GeoClue)");
    if (cfg.geo.provider == GEO_AUTO || cfg.geo.provider == GEO_GPSD)
      printf("gpsd:        %s:%d\n", cfg.geo.gpsd_host, cfg.geo.gpsd_port);
    return end_edit(&cfg, lock, false);
  }
  if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0 ||
      strcmp(sub, "toggle") == 0) {
    cfg.geo.enabled = strcmp(sub, "toggle") == 0 ? !cfg.geo.enabled
                                                 : strcmp(sub, "on") == 0;
    if (cfg.geo.enabled)
      cfg.active = -1;
    printf("Geolocation %s\n", cfg.geo.enabled ? "enabled" : "disabled");
    return end_edit(&cfg, lock, true);
  }
  if (strcmp(sub, "provider") == 0) {
    int p = argc > 2 ? unit_lookup(GEO_PROVIDER_IDS, GEO_PROVIDER_COUNT,
                                   argv[2])
                     : -1;
    if (p < 0) {
      end_edit(&cfg, lock, false);
      return usage_error("geo", "Provider must be auto, gpsd, geoclue or ip");
    }
    if (p == GEO_GEOCLUE && !geolocate_has_geoclue())
      log_warn("This build has no GeoClue support");
    cfg.geo.provider = p;
    printf("Geolocation provider: %s\n", GEO_PROVIDER_IDS[p]);
    return end_edit(&cfg, lock, true);
  }
  if (strcmp(sub, "locate") == 0 || strcmp(sub, "save") == 0) {
    location_t loc = cfg.current;
    loc.name[0] = '\0';
    char source[64], err[512];
    if (!geolocate(&cfg.geo, true, &loc, source, sizeof source, err,
                   sizeof err)) {
      log_error("%s", err);
      return end_edit(&cfg, lock, false) | 1;
    }
    char place[512];
    location_place(&loc, place, sizeof place);
    printf("%s (%.4f, %.4f) via %s\n", place, loc.latitude, loc.longitude,
           source);
    if (strcmp(sub, "locate") == 0)
      return end_edit(&cfg, lock, false);
    if (argc > 2)
      str_copy(loc.name, argv[2], sizeof loc.name);
    loc.is_default = false;
    int idx = config_add(&cfg, &loc);
    if (idx < 0) {
      log_error("Too many saved locations");
      return end_edit(&cfg, lock, false) | 1;
    }
    printf("Saved as #%d\n", idx + 1);
    return end_edit(&cfg, lock, true);
  }
  end_edit(&cfg, lock, false);
  return usage_error("geo", "Unknown geo command");
}

/* ---------- refresh / reset / config ---------- */

static int cmd_refresh(void) {
  int n = daemon_signal_all(SIGUSR1);
  if (n == 0) {
    log_warn("No running wayweather daemon found");
    return 1;
  }
  printf("Asked %d daemon%s to refresh\n", n, n == 1 ? "" : "s");
  return 0;
}

static int cmd_reset(int argc, char **argv) {
  static const struct option opts[] = {{"units", no_argument, 0, 'u'},
                                       {"all", no_argument, 0, 'a'},
                                       {"yes", no_argument, 0, 'y'},
                                       {"help", no_argument, 0, 'h'},
                                       {0, 0, 0, 0}};
  bool units = false, all = false, yes = false;
  int c;
  while ((c = getopt_long(argc, argv, "uayh", opts, NULL)) != -1) {
    switch (c) {
    case 'u':
      units = true;
      break;
    case 'a':
      all = true;
      break;
    case 'y':
      yes = true;
      break;
    case 'h':
      help_command(stdout, "reset");
      return 0;
    default:
      return usage_error("reset", "Invalid option");
    }
  }
  config_t cfg;
  int lock;
  if (!begin_edit(&cfg, &lock))
    return 1;
  if (all) {
    if (!yes && !confirm("Reset ALL settings and delete every saved "
                         "location?")) {
      printf("Nothing changed.\n");
      return end_edit(&cfg, lock, false);
    }
    config_free(&cfg);
    config_defaults(&cfg);
    printf("Config reset to defaults.\n");
    return end_edit(&cfg, lock, true);
  }
  units_t keep = cfg.current.units;
  location_init(&cfg.current);
  if (!units)
    cfg.current.units = keep;
  cfg.active = -1;
  cfg.geo.enabled = true;
  printf("Current location cleared, geolocation enabled%s.\n",
         units ? ", units reset to metric" : "");
  return end_edit(&cfg, lock, true);
}

static int cmd_config(int argc, char **argv) {
  const char *sub = argc > 1 ? argv[1] : "path";
  if (strcmp(sub, "path") == 0) {
    puts(config_path());
    return 0;
  }
  if (strcmp(sub, "show") == 0 || strcmp(sub, "check") == 0) {
    config_t cfg;
    if (!config_load(&cfg))
      return 1;
    if (strcmp(sub, "show") == 0) {
      char *s = config_to_json(&cfg);
      puts(s);
      free(s);
    } else {
      printf("%s is valid (%d saved location%s)\n", config_path(),
             cfg.n_locations, cfg.n_locations == 1 ? "" : "s");
    }
    config_free(&cfg);
    return 0;
  }
  if (strcmp(sub, "edit") == 0) {
    config_t cfg;
    config_load(&cfg); /* make sure it exists */
    config_free(&cfg);
    const char *editor = getenv("VISUAL");
    if (str_empty(editor))
      editor = getenv("EDITOR");
    if (str_empty(editor))
      editor = "vi";
    char *cmd = xasprintf("%s '%s'", editor, config_path());
    int rc = system(cmd);
    free(cmd);
    if (rc != 0)
      log_warn("Editor exited with status %d", rc);
    if (!config_load(&cfg)) {
      log_error("The config is not valid anymore, run \"wayweather config "
                "edit\" again to fix it");
      return 1;
    }
    config_free(&cfg);
    printf("Config OK.\n");
    return 0;
  }
  if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0) {
    help_command(stdout, "config");
    return 0;
  }
  return usage_error("config", "Use path, show, edit or check");
}

/* ---------- Dispatch ---------- */

typedef struct {
  const char *flag;
  const char *command;
} alias;

/* Flags of wayweather 1.x/2.x mapped to the new commands */
static const alias LEGACY[] = {
    {"-g", "get"},          {"--get", "get"},     {"-p", "print"},
    {"--print", "print"},   {"-w", "daemon"},     {"--daemon", "daemon"},
    {"-s", "set"},          {"--set", "set"},     {"-r", "reset"},
    {"--reset", "reset"},   {"-l", "use"},        {"--load", "use"},
    {"--list", "list"},     {"-sd", "default"},   {"--set-default", "default"},
    {"-d", "remove"},       {"--delete", "remove"}, {"-q", "tui"},
    {"--gui", "tui"},       {"--tui", "tui"},     {"ls", "list"},
    {"rm", "remove"},       {"delete", "remove"}, {"load", "use"},
    {"previous", "prev"},   {"show", "print"},    {"gui", "tui"},
    {"waybar", "get"},
};

static int run_command(const char *cmd, int argc, char **argv) {
  optind = 1;
  if (strcmp(cmd, "get") == 0)
    return cmd_show(argc, argv, true);
  if (strcmp(cmd, "print") == 0)
    return cmd_show(argc, argv, false);
  if (strcmp(cmd, "daemon") == 0)
    return cmd_daemon(argc, argv);
  if (strcmp(cmd, "tui") == 0)
    return tui_run();
  if (strcmp(cmd, "search") == 0)
    return cmd_search(argc, argv);
  if (strcmp(cmd, "add") == 0)
    return cmd_add(argc, argv);
  if (strcmp(cmd, "list") == 0)
    return cmd_list(argc, argv);
  if (strcmp(cmd, "remove") == 0)
    return cmd_remove(argc, argv);
  if (strcmp(cmd, "edit") == 0)
    return cmd_edit(argc, argv);
  if (strcmp(cmd, "use") == 0)
    return cmd_use(argc, argv);
  if (strcmp(cmd, "default") == 0)
    return cmd_default(argc, argv);
  if (strcmp(cmd, "next") == 0)
    return cmd_cycle(1);
  if (strcmp(cmd, "prev") == 0)
    return cmd_cycle(-1);
  if (strcmp(cmd, "set") == 0)
    return cmd_set(argc, argv);
  if (strcmp(cmd, "units") == 0)
    return cmd_units(argc, argv);
  if (strcmp(cmd, "geo") == 0)
    return cmd_geo(argc, argv);
  if (strcmp(cmd, "refresh") == 0)
    return cmd_refresh();
  if (strcmp(cmd, "reset") == 0)
    return cmd_reset(argc, argv);
  if (strcmp(cmd, "config") == 0)
    return cmd_config(argc, argv);
  if (strcmp(cmd, "help") == 0) {
    if (argc > 1 && !help_command(stdout, argv[1])) {
      log_error("No help for \"%s\"", argv[1]);
      return 2;
    }
    if (argc <= 1)
      help_main(stdout);
    return 0;
  }
  if (strcmp(cmd, "version") == 0) {
    help_version(stdout);
    return 0;
  }
  log_error("Unknown command \"%s\"", cmd);
  fprintf(stderr, "Run \"wayweather help\" for a list of commands.\n");
  return 2;
}

int main(int argc, char **argv) {
  /* UTF-8 for the TUI, but always "." as the decimal separator */
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");
  log_init();
  use_color = isatty(STDOUT_FILENO) && str_empty(getenv("NO_COLOR"));

  /* Global options come before the command */
  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i++) {
    const char *a = argv[i];
    if (strcmp(a, "-c") == 0 || strcmp(a, "--config") == 0) {
      if (i + 1 >= argc)
        return usage_error("", "--config needs a file");
      config_set_path(argv[++i]);
    } else if (strncmp(a, "--config=", 9) == 0) {
      config_set_path(a + 9);
    } else if (strcmp(a, "--verbose") == 0) {
      log_level = log_level < LOG_INFO ? LOG_INFO : LOG_DEBUG;
    } else if (strcmp(a, "--debug") == 0) {
      log_level = LOG_DEBUG;
    } else if (strcmp(a, "--no-color") == 0) {
      use_color = false;
      log_set_color(false);
    } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      help_set_color(use_color);
      /* legacy: "wayweather -h -h" */
      if (i + 1 < argc && (strcmp(argv[i + 1], "-h") == 0 ||
                           strcmp(argv[i + 1], "--help") == 0))
        printf("Not enough help? See the wiki: %s/wiki\n", WW_URL);
      else
        help_main(stdout);
      return 0;
    } else if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
      help_version(stdout);
      return 0;
    } else {
      break; /* a legacy command flag such as --get */
    }
  }
  help_set_color(use_color);

  const char *cmd;
  if (i >= argc) {
    cmd = interactive() ? "tui" : "get";
  } else {
    cmd = argv[i];
    for (size_t k = 0; k < ARRAY_LEN(LEGACY); k++)
      if (strcmp(cmd, LEGACY[k].flag) == 0)
        cmd = LEGACY[k].command;
  }

  /* Pass the command and its arguments; argv[0] becomes the command */
  int sub_argc = i < argc ? argc - i : 1;
  char *fallback[] = {(char *)cmd, NULL};
  char **sub_argv = i < argc ? argv + i : fallback;
  if (i < argc)
    sub_argv[0] = (char *)cmd;

  /* Allow "wayweather COMMAND --help" for every command */
  for (int k = 1; k < sub_argc; k++) {
    if (strcmp(sub_argv[k], "--help") == 0 && strcmp(cmd, "help") != 0) {
      if (!help_command(stdout, cmd))
        help_main(stdout);
      return 0;
    }
  }

  http_init();
  int rc = run_command(cmd, sub_argc, sub_argv);
  http_cleanup();
  return rc;
}
