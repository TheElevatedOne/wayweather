#define _GNU_SOURCE
#include "tui.h"
#include "app.h"
#include "config.h"
#include "geocode.h"
#include "geolocate.h"
#include "log.h"
#include "render.h"
#include "ui.h"
#include "units.h"
#include "util.h"
#include "version.h"
#include "weather.h"

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIN_COLS 50
#define MIN_LINES 16
#define STATUS_MS 6000
#define PREVIEW_SLOTS 24
#define PREVIEW_MAX_AGE_MS (10LL * 60 * 1000)

/* ======================================================================
 * Background weather previews
 *
 * Network requests run on a worker thread so the interface never blocks.
 * The UI asks for a preview every frame; missing or old ones are queued
 * and the screen updates when they arrive.
 * ====================================================================== */

typedef struct {
  char key[96];
  bool used;
  bool pending;
  bool ok;
  bool geo; /* resolve the position via geolocation first */
  location_t loc;
  weather_t w;
  char source[64];
  char err[256];
  long long when;
} preview;

static struct {
  pthread_t thread;
  pthread_mutex_t mu;
  pthread_cond_t cv;
  preview slots[PREVIEW_SLOTS];
  int want; /* slot to fetch next, -1 = idle */
  bool want_force;
  bool busy;
  bool stop;
  bool updated;
  geo_config geo;
  int cache_ttl;
} pv = {.mu = PTHREAD_MUTEX_INITIALIZER,
        .cv = PTHREAD_COND_INITIALIZER,
        .want = -1};

static void *preview_worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&pv.mu);
  for (;;) {
    while (!pv.stop && pv.want < 0)
      pthread_cond_wait(&pv.cv, &pv.mu);
    if (pv.stop)
      break;
    int i = pv.want;
    bool force = pv.want_force;
    pv.want = -1;
    pv.busy = true;
    char key[96];
    str_copy(key, pv.slots[i].key, sizeof key);
    location_t loc = pv.slots[i].loc;
    bool geo = pv.slots[i].geo;
    geo_config geocfg = pv.geo;
    int ttl = pv.cache_ttl;
    pthread_mutex_unlock(&pv.mu);

    char err[256] = "", source[64] = "";
    weather_t *w = xmalloc(sizeof *w);
    bool ok = true;
    if (geo)
      ok = geolocate(&geocfg, force, &loc, source, sizeof source, err,
                     sizeof err);
    if (ok)
      ok = weather_get(loc.latitude, loc.longitude, &loc.units, ttl, force, w,
                       err, sizeof err);

    pthread_mutex_lock(&pv.mu);
    pv.busy = false;
    preview *p = &pv.slots[i];
    if (p->used && strcmp(p->key, key) == 0) {
      p->pending = false;
      p->when = now_ms();
      if (ok) {
        p->ok = true;
        p->w = *w;
        p->loc = loc;
        p->err[0] = '\0';
        str_copy(p->source, source, sizeof p->source);
      } else if (!p->ok) {
        str_copy(p->err, err, sizeof p->err);
      } else {
        /* keep showing the older data, but remember the failure */
        str_copy(p->err, err, sizeof p->err);
      }
      pv.updated = true;
    }
    free(w);
  }
  pthread_mutex_unlock(&pv.mu);
  return NULL;
}

static void preview_start(const config_t *cfg) {
  pv.geo = cfg->geo;
  pv.cache_ttl = cfg->cache_ttl;
  pthread_create(&pv.thread, NULL, preview_worker, NULL);
}

/* Returns true when the worker could be stopped cleanly */
static bool preview_stop(void) {
  pthread_mutex_lock(&pv.mu);
  pv.stop = true;
  bool busy = pv.busy;
  pthread_cond_signal(&pv.cv);
  pthread_mutex_unlock(&pv.mu);
  if (busy)
    return false;
  pthread_join(pv.thread, NULL);
  return true;
}

static void preview_key(const location_t *loc, bool geo, char *key,
                        size_t size) {
  if (geo)
    snprintf(key, size, "geo:%d%d%d", loc->units.temperature, loc->units.wind,
             loc->units.precipitation);
  else
    snprintf(key, size, "%.4f,%.4f:%d%d%d", loc->latitude, loc->longitude,
             loc->units.temperature, loc->units.wind,
             loc->units.precipitation);
}

/* Copies the preview for `loc` into `out`, queueing a fetch if needed */
static void preview_get(const config_t *cfg, const location_t *loc, bool geo,
                        bool force, preview *out) {
  char key[96];
  preview_key(loc, geo, key, sizeof key);
  pthread_mutex_lock(&pv.mu);
  pv.geo = cfg->geo;
  pv.cache_ttl = cfg->cache_ttl;

  int slot = -1, oldest = 0;
  for (int i = 0; i < PREVIEW_SLOTS; i++) {
    if (pv.slots[i].used && strcmp(pv.slots[i].key, key) == 0) {
      slot = i;
      break;
    }
    if (!pv.slots[i].used ||
        (pv.slots[oldest].used && pv.slots[i].when < pv.slots[oldest].when))
      oldest = i;
  }
  if (slot < 0) {
    slot = oldest;
    if (pv.want == slot)
      pv.want = -1;
    memset(&pv.slots[slot], 0, sizeof pv.slots[slot]);
    pv.slots[slot].used = true;
    str_copy(pv.slots[slot].key, key, sizeof pv.slots[slot].key);
  }
  preview *p = &pv.slots[slot];
  /* `when` is the completion time of the last attempt, 0 = never tried */
  bool stale = p->when == 0 || now_ms() - p->when > PREVIEW_MAX_AGE_MS;
  if ((force || stale) && !(p->pending && !force)) {
    /* Only the latest request matters: unqueue an older one. It is
     * requested again when it becomes visible. */
    if (pv.want >= 0 && pv.want != slot)
      pv.slots[pv.want].pending = false;
    p->pending = true;
    p->geo = geo;
    p->loc = *loc;
    pv.want = slot;
    pv.want_force = force;
    pthread_cond_signal(&pv.cv);
  }
  *out = *p;
  pthread_mutex_unlock(&pv.mu);
  /* Pressure and clock are formatted locally: always use the latest */
  out->w.units = loc->units;
  if (!geo)
    out->loc = *loc;
  else {
    location_t resolved = out->loc;
    out->loc = *loc;
    out->loc.latitude = resolved.latitude;
    out->loc.longitude = resolved.longitude;
    str_copy(out->loc.city, resolved.city, sizeof out->loc.city);
    str_copy(out->loc.region, resolved.region, sizeof out->loc.region);
    str_copy(out->loc.country, resolved.country, sizeof out->loc.country);
    out->loc.name[0] = '\0';
  }
}

static bool preview_take_update(void) {
  pthread_mutex_lock(&pv.mu);
  bool u = pv.updated;
  pv.updated = false;
  pthread_mutex_unlock(&pv.mu);
  return u;
}

/* Forget cached geolocation previews (provider or units changed) */
static void preview_drop_geo(void) {
  pthread_mutex_lock(&pv.mu);
  for (int i = 0; i < PREVIEW_SLOTS; i++)
    if (pv.slots[i].used && pv.slots[i].geo && pv.want != i)
      pv.slots[i].used = false;
  pthread_mutex_unlock(&pv.mu);
}

/* ======================================================================
 * State
 * ====================================================================== */

typedef enum { TAB_LOCATIONS, TAB_CURRENT, TAB_SETTINGS, TAB_COUNT } tab_id;
static const char *const TAB_NAMES[TAB_COUNT] = {"Locations", "Current",
                                                 "Settings"};

typedef struct {
  config_t cfg;
  tab_id tab;

  /* Locations tab */
  int view[MAX_LOCATIONS]; /* indices of locations passing the filter */
  int n_view;
  int sel; /* position in view */
  int top;
  bool filtering;
  line_edit filter;
  char filter_text[256];

  /* Settings tab */
  int set_sel;
  int set_top;

  /* Undo for the last deletion */
  bool has_undo;
  location_t undo_loc;
  int undo_idx;
  bool undo_active;

  char status[256];
  int status_cp;
  long long status_at;

  struct timespec cfg_mtime;
  long long last_check;
  bool quit;

  /* Layout, for mouse handling */
  int tab_x[TAB_COUNT + 1];
  int list_first_row;
  int list_rows;
  int list_x0, list_x1;
} tui_state;

static void status(tui_state *t, int cp, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void status(tui_state *t, int cp, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(t->status, sizeof t->status, fmt, ap);
  va_end(ap);
  t->status_cp = cp;
  t->status_at = now_ms();
}

static struct timespec file_mtime(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return (struct timespec){0, 0};
  return st.st_mtim;
}

static bool save(tui_state *t) {
  int lock = config_lock();
  bool ok = config_save(&t->cfg);
  config_unlock(lock);
  t->cfg_mtime = file_mtime(config_path());
  if (!ok) {
    const char *why = log_last();
    status(t, CP_ERROR, "Could not save the config%s%s", why[0] ? ": " : "",
           why);
  }
  return ok;
}

static void rebuild_view(tui_state *t) {
  t->n_view = 0;
  for (int i = 0; i < t->cfg.n_locations; i++) {
    const location_t *l = &t->cfg.locations[i];
    const char *f = t->filter_text;
    if (!f[0] || str_icontains(l->name, f) || str_icontains(l->city, f) ||
        str_icontains(l->region, f) || str_icontains(l->country, f))
      t->view[t->n_view++] = i;
  }
  if (t->sel >= t->n_view)
    t->sel = t->n_view - 1;
  if (t->sel < 0)
    t->sel = 0;
}

static int selected_index(const tui_state *t) {
  return t->n_view > 0 ? t->view[t->sel] : -1;
}

static void select_index(tui_state *t, int idx) {
  for (int i = 0; i < t->n_view; i++) {
    if (t->view[i] == idx) {
      t->sel = i;
      return;
    }
  }
  /* Not visible: clear the filter */
  t->filter_text[0] = '\0';
  le_set(&t->filter, "");
  rebuild_view(t);
  for (int i = 0; i < t->n_view; i++)
    if (t->view[i] == idx)
      t->sel = i;
}

/* The saved location currently shown in Waybar, or -1 */
static int shown_index(const tui_state *t) {
  return t->cfg.geo.enabled ? -1 : config_match_current(&t->cfg);
}

static void reload_if_changed(tui_state *t) {
  if (now_ms() - t->last_check < 1000)
    return;
  t->last_check = now_ms();
  struct timespec m = file_mtime(config_path());
  if (m.tv_sec == t->cfg_mtime.tv_sec && m.tv_nsec == t->cfg_mtime.tv_nsec)
    return;
  config_t fresh;
  if (!config_load(&fresh)) {
    config_free(&fresh);
    t->cfg_mtime = m;
    status(t, CP_ERROR, "The config file was changed and is now invalid");
    return;
  }
  int keep = selected_index(t);
  config_free(&t->cfg);
  t->cfg = fresh;
  t->cfg_mtime = m;
  t->has_undo = false;
  rebuild_view(t);
  if (keep >= 0 && keep < t->cfg.n_locations)
    select_index(t, keep);
  status(t, CP_ACCENT, "Config reloaded (changed outside the TUI)");
}

/* ======================================================================
 * Drawing helpers
 * ====================================================================== */

static void draw_box(int y, int x, int h, int w, const char *title,
                     bool focused) {
  if (h < 2 || w < 2)
    return;
  attr_t a = COLOR_PAIR(focused ? CP_ACCENT : CP_BORDER);
  ui_box(stdscr, y, x, h, w, a);
  if (title && *title) {
    char t[256];
    snprintf(t, sizeof t, " %s ", title);
    ui_put(stdscr, y, x + 2, w - 4, a | A_BOLD, t);
  }
}

/* "key  value" row; returns next y */
static int kv(int y, int x, int w, const char *key, attr_t va,
              const char *value) {
  ui_put(stdscr, y, x, w, A_DIM, key);
  ui_put(stdscr, y, x + 11, w - 11, va, value);
  return y + 1;
}

typedef struct {
  const char *key;
  const char *desc;
} hint;

static void draw_hints(const hint *hints, int n) {
  int y = LINES - 1, x = 0;
  ui_fill(stdscr, y, 0, COLS, A_REVERSE);
  for (int i = 0; i < n && x < COLS - 2; i++) {
    x += ui_put(stdscr, y, x, COLS - x, A_REVERSE | A_BOLD, " ");
    x += ui_put(stdscr, y, x, COLS - x, A_REVERSE | A_BOLD, hints[i].key);
    x += ui_put(stdscr, y, x, COLS - x, A_REVERSE, " ");
    x += ui_put(stdscr, y, x, COLS - x, A_REVERSE, hints[i].desc);
    x += ui_put(stdscr, y, x, COLS - x, A_REVERSE, " ");
  }
}

static attr_t temp_attr(const weather_t *w, double v) {
  double c = temp_to_celsius(v, w->units.temperature);
  if (c >= 25)
    return COLOR_PAIR(CP_HOT) | A_BOLD;
  if (c <= 2)
    return COLOR_PAIR(CP_COLD) | A_BOLD;
  return A_BOLD;
}

static int put_tmpl(int y, int x, int w, attr_t a, const render_ctx *ctx,
                    const char *tmpl) {
  char *s = render_template(ctx, tmpl);
  ui_put(stdscr, y, x, w, a, s);
  free(s);
  return y + 1;
}

/* Weather summary used by the Locations and Current tabs; returns next y */
static int draw_weather(const tui_state *t, const preview *p, int y, int x,
                        int w, int bottom) {
  bool icons = t->cfg.display.icons;
  if (!p->ok) {
    if (p->pending) {
      ui_put(stdscr, y, x, w, A_DIM, "Fetching weather…");
    } else if (p->err[0]) {
      ui_put(stdscr, y++, x, w, COLOR_PAIR(CP_ERROR) | A_BOLD,
             "Weather unavailable");
      int n = 0;
      const char *e = p->err;
      while (*e && y < bottom && n++ < 3) {
        size_t fit = utf8_fit(e, w);
        char line[512];
        str_copy(line, e, fit + 1 < sizeof line ? fit + 1 : sizeof line);
        ui_put(stdscr, y++, x, w, COLOR_PAIR(CP_ERROR), line);
        e += fit;
      }
      if (y < bottom)
        ui_put(stdscr, y++, x, w, A_DIM, "Press r to retry");
    }
    return y;
  }

  const weather_t *wx = &p->w;
  render_ctx ctx = {&t->cfg, &p->loc, wx, p->source, icons};
  char temp[32];
  fmt_number(temp, sizeof temp, wx->temperature, t->cfg.display.decimals);
  int cx = x;
  if (icons)
    cx += ui_putf(stdscr, y, cx, w, COLOR_PAIR(CP_ACCENT) | A_BOLD, "%s  ",
                  wmo_icon(wx->code, wx->is_day));
  cx += ui_putf(stdscr, y, cx, x + w - cx, temp_attr(wx, wx->temperature),
                "%s%s", temp, TEMP_LABELS[wx->units.temperature]);
  cx += ui_putf(stdscr, y, cx, x + w - cx, A_BOLD, "  %s",
                wmo_description(wx->code));
  if (p->pending)
    ui_put(stdscr, y, cx, x + w - cx, A_DIM, "  (updating…)");
  y++;
  if (wx->stale && y < bottom)
    ui_put(stdscr, y++, x, w, COLOR_PAIR(CP_WARN),
           "Offline — showing cached data");
  else if (p->err[0] && y < bottom)
    ui_put(stdscr, y++, x, w, COLOR_PAIR(CP_WARN), "Last refresh failed");

  static const char *const lines[] = {
      "Feels like {feels}{tunit} · Today {tmin}° – {tmax}°",
      "Humidity {humidity}% · Clouds {cloud}% · Precip. {precip} {punit}",
      "Wind {wind} {wunit} {wind_dir} {wind_arrow} · Gusts {gust} {wunit}",
      "Pressure {pressure} {presunit} · UV {uv} · Visibility {visibility}",
      "Sunrise {sunrise} · Sunset {sunset} · Updated {updated}",
  };
  for (size_t i = 0; i < ARRAY_LEN(lines) && y < bottom; i++)
    y = put_tmpl(y, x, w, 0, &ctx, lines[i]);

  /* Hourly strip: as many columns as fit */
  int colw = wx->units.clock == CLOCK_12H ? 9 : 7;
  int ncols = w / colw;
  if (ncols > wx->n_hours)
    ncols = wx->n_hours;
  if (ncols > 12)
    ncols = 12;
  if (ncols > 0 && y + 4 < bottom) {
    y++;
    for (int i = 0; i < ncols; i++) {
      const weather_hour *h = &wx->hours[i];
      char clock[16], tv[16];
      fmt_clock(clock, sizeof clock, h->time, wx->units.clock);
      if (wx->units.clock == CLOCK_12H) {
        /* "2:00 PM" -> "2 PM" */
        char *colon = strchr(clock, ':');
        if (colon)
          memmove(colon, colon + 3, strlen(colon + 3) + 1);
      }
      fmt_number(tv, sizeof tv, h->temperature, 0);
      int cxx = x + i * colw;
      ui_put(stdscr, y, cxx, colw, A_DIM, clock);
      if (icons)
        ui_put(stdscr, y + 1, cxx + 1, colw, COLOR_PAIR(CP_ACCENT),
               wmo_icon(h->code, h->is_day));
      ui_putf(stdscr, y + (icons ? 2 : 1), cxx, colw,
              temp_attr(wx, h->temperature), "%s°", tv);
      ui_putf(stdscr, y + (icons ? 3 : 2), cxx, colw,
              COLOR_PAIR(CP_COLD), "%d%%", h->precip_probability);
    }
    y += icons ? 4 : 3;
  }

  /* Daily forecast */
  if (y + 1 < bottom && wx->n_days > 0)
    y++;
  for (int i = 0; i < wx->n_days && y < bottom; i++) {
    const weather_day *d = &wx->days[i];
    char day[32], lo[16], hi[16];
    fmt_day(day, sizeof day, wx->days, i);
    fmt_number(lo, sizeof lo, d->temp_min, 0);
    fmt_number(hi, sizeof hi, d->temp_max, 0);
    int dx = x;
    dx += ui_putf(stdscr, y, dx, w, A_BOLD, "%-9s", day);
    if (icons)
      dx += ui_putf(stdscr, y, dx, x + w - dx, COLOR_PAIR(CP_ACCENT), "%s  ",
                    wmo_icon(d->code, true));
    dx += ui_putf(stdscr, y, dx, x + w - dx, temp_attr(wx, d->temp_min),
                  "%4s°", lo);
    dx += ui_put(stdscr, y, dx, x + w - dx, A_DIM, " / ");
    dx += ui_putf(stdscr, y, dx, x + w - dx, temp_attr(wx, d->temp_max),
                  "%3s°", hi);
    dx += ui_putf(stdscr, y, dx, x + w - dx, COLOR_PAIR(CP_COLD), "  %3d%%",
                  d->precip_probability);
    ui_putf(stdscr, y, dx, x + w - dx, A_DIM, "  %s",
            wmo_description(d->code));
    y++;
  }
  return y;
}

/* ======================================================================
 * Header / footer
 * ====================================================================== */

static void draw_header(tui_state *t) {
  ui_fill(stdscr, 0, 0, COLS, COLOR_PAIR(CP_TITLE));
  int x = ui_putf(stdscr, 0, 1, COLS - 2, COLOR_PAIR(CP_TITLE) | A_BOLD,
                  "%swayweather %s", t->cfg.display.icons ? "\U000F0599 " : "",
                  WW_VERSION);

  char showing[256];
  if (t->cfg.geo.enabled) {
    snprintf(showing, sizeof showing, "Showing: geolocation (%s)",
             GEO_PROVIDER_IDS[t->cfg.geo.provider]);
  } else if (app_location_set(&t->cfg.current)) {
    char label[FIELD_LEN];
    location_label(&t->cfg.current, label, sizeof label);
    snprintf(showing, sizeof showing, "Showing: %s", label);
  } else {
    snprintf(showing, sizeof showing, "No location set");
  }
  int sw = utf8_width(showing);
  if (sw + x + 4 < COLS)
    ui_put_right(stdscr, 0, COLS - 1, COLOR_PAIR(CP_TITLE), showing);

  /* Tabs */
  int tx = 1;
  for (int i = 0; i < TAB_COUNT; i++) {
    char label[64];
    snprintf(label, sizeof label, " %d %s ", i + 1, TAB_NAMES[i]);
    attr_t a = i == (int)t->tab ? (COLOR_PAIR(CP_SELECTED) | A_BOLD) : A_DIM;
    t->tab_x[i] = tx;
    tx += ui_put(stdscr, 1, tx, COLS - tx, a, label) + 1;
  }
  t->tab_x[TAB_COUNT] = tx;
}

static void draw_status(const tui_state *t) {
  if (t->status[0] && now_ms() - t->status_at < STATUS_MS)
    ui_put(stdscr, LINES - 2, 1, COLS - 2, COLOR_PAIR(t->status_cp) | A_BOLD,
           t->status);
}

/* ======================================================================
 * Locations tab
 * ====================================================================== */

static void draw_locations(tui_state *t) {
  int y0 = 2, h = LINES - 4;
  int lw = COLS >= 100 ? COLS * 40 / 100 : COLS >= 76 ? 34 : COLS;
  bool details = lw < COLS;

  char title[128];
  if (t->filter_text[0] || t->filtering)
    snprintf(title, sizeof title, "Saved locations · %d of %d", t->n_view,
             t->cfg.n_locations);
  else
    snprintf(title, sizeof title, "Saved locations · %d",
             t->cfg.n_locations);
  draw_box(y0, 0, h, lw, title, true);

  int row = y0 + 1;
  int inner = lw - 2;
  if (t->filtering || t->filter_text[0]) {
    ui_put(stdscr, row, 2, 2, COLOR_PAIR(CP_ACCENT) | A_BOLD, "/");
    if (!t->filtering)
      ui_put(stdscr, row, 4, inner - 4, A_UNDERLINE, t->filter_text);
    row++;
  }
  int rows = y0 + h - 1 - row;
  t->list_first_row = row;
  t->list_rows = rows;
  t->list_x0 = 1;
  t->list_x1 = lw - 1;

  if (t->sel < t->top)
    t->top = t->sel;
  if (t->sel >= t->top + rows)
    t->top = t->sel - rows + 1;
  if (t->top < 0)
    t->top = 0;

  int shown = shown_index(t);
  if (t->cfg.n_locations == 0) {
    int cy = row + rows / 2 - 3;
    ui_put_center(stdscr, cy++, lw, A_BOLD, "No saved locations yet");
    cy++;
    ui_put_center(stdscr, cy++, lw, 0, "Press a to search for a city");
    ui_put_center(stdscr, cy++, lw, 0, "or n to enter coordinates");
    cy++;
    ui_put_center(stdscr, cy, lw, A_DIM, "s saves the current location");
  } else if (t->n_view == 0) {
    ui_put_center(stdscr, row + 1, lw, A_DIM, "Nothing matches the filter");
  }

  for (int i = 0; i < rows && t->top + i < t->n_view; i++) {
    int pos = t->top + i;
    int idx = t->view[pos];
    const location_t *l = &t->cfg.locations[idx];
    bool selected = pos == t->sel;
    attr_t base = selected ? COLOR_PAIR(CP_SELECTED) : 0;
    int y = row + i;
    ui_fill(stdscr, y, 1, inner, base);

    char label[FIELD_LEN];
    location_label(l, label, sizeof label);
    int x = 1;
    x += ui_putf(stdscr, y, x, inner, base | A_DIM, "%3d ", idx + 1);
    x += ui_put(stdscr, y, x, 2,
                base | (selected ? A_BOLD : COLOR_PAIR(CP_WARN) | A_BOLD),
                l->is_default ? "★" : " ");
    x += ui_put(stdscr, y, x, 2,
                base | (selected ? A_BOLD : COLOR_PAIR(CP_OK) | A_BOLD),
                idx == shown ? "●" : " ");
    x++;
    const char *right = !str_empty(l->country) ? l->country : l->region;
    int rw = utf8_width(right);
    int avail = inner - (x - 1) - 1;
    if (rw + 2 > avail / 2)
      rw = 0; /* not enough room for the country */
    ui_put(stdscr, y, x, avail - (rw ? rw + 2 : 0), base | A_BOLD, label);
    if (rw)
      ui_put_right(stdscr, y, lw - 2, base | (selected ? 0 : A_DIM), right);
  }
  if (t->top > 0)
    ui_put(stdscr, row, lw - 2, 1, A_DIM, "↑");
  if (t->top + rows < t->n_view)
    ui_put(stdscr, row + rows - 1, lw - 2, 1, A_DIM, "↓");

  if (details) {
    int dx = lw, dw = COLS - lw;
    int idx = selected_index(t);
    if (idx < 0) {
      draw_box(y0, dx, h, dw, "Details", false);
      int y = y0 + 2, x = dx + 2, w = dw - 4;
      ui_put(stdscr, y++, x, w, A_BOLD, "Getting started");
      y++;
      ui_put(stdscr, y++, x, w, 0, "a  search a city and save it");
      ui_put(stdscr, y++, x, w, 0, "n  add coordinates manually");
      ui_put(stdscr, y++, x, w, 0, "g  toggle GPS / IP geolocation");
      ui_put(stdscr, y++, x, w, 0, "?  all key bindings");
    } else {
      const location_t *l = &t->cfg.locations[idx];
      char label[FIELD_LEN], place[512], units[96], coords[64];
      location_label(l, label, sizeof label);
      location_place(l, place, sizeof place);
      units_describe(&l->units, units, sizeof units);
      snprintf(coords, sizeof coords, "%.4f, %.4f", l->latitude,
               l->longitude);
      draw_box(y0, dx, h, dw, label, false);
      int y = y0 + 1, x = dx + 2, w = dw - 4;
      if (!str_empty(l->name))
        y = kv(y, x, w, "Name", A_BOLD, l->name);
      y = kv(y, x, w, "Place", 0, place);
      y = kv(y, x, w, "Coords", 0, coords);
      char ubuf[128];
      snprintf(ubuf, sizeof ubuf, "%s (%s)", units,
               units_preset_name(&l->units));
      y = kv(y, x, w, "Units", 0, ubuf);
      char flags[128] = "";
      if (l->is_default)
        strcat(flags, "★ default  ");
      if (idx == shown)
        strcat(flags, "● shown in Waybar");
      if (flags[0])
        y = kv(y, x, w, "Status", COLOR_PAIR(CP_OK) | A_BOLD, flags);
      ui_rule(stdscr, y++, dx, dw, COLOR_PAIR(CP_BORDER));
      preview p;
      preview_get(&t->cfg, l, false, false, &p);
      draw_weather(t, &p, y, x, w, y0 + h - 1);
    }
  }
}

/* ---------- Location dialogs ---------- */

typedef struct {
  char lat[32];
  char lon[32];
  location_t *loc;
} loc_form_data;

static bool validate_location(form_field *f, int n, void *user, char *err,
                              size_t errlen) {
  (void)f;
  (void)n;
  loc_form_data *d = user;
  location_t tmp = *d->loc;
  if (!parse_double(d->lat, &tmp.latitude)) {
    snprintf(err, errlen, "Latitude must be a number, e.g. 48.1486");
    return false;
  }
  if (!parse_double(d->lon, &tmp.longitude)) {
    snprintf(err, errlen, "Longitude must be a number, e.g. 17.1077");
    return false;
  }
  if (!location_validate(&tmp, err, errlen))
    return false;
  *d->loc = tmp;
  return true;
}

/* Edits `loc` in a form; true if saved */
static bool location_dialog(const char *title, location_t *loc,
                            bool with_default) {
  location_t tmp = *loc;
  loc_form_data d = {.loc = &tmp};
  snprintf(d.lat, sizeof d.lat, "%.5f", tmp.latitude);
  snprintf(d.lon, sizeof d.lon, "%.5f", tmp.longitude);
  int temp = tmp.units.temperature, wind = tmp.units.wind,
      precip = tmp.units.precipitation, pres = tmp.units.pressure,
      clock = tmp.units.clock;
  bool def = tmp.is_default;

  form_field f[] = {
      {.label = "Name", .type = FF_TEXT, .text = tmp.name,
       .text_size = sizeof tmp.name,
       .hint = "Optional label shown instead of the city, e.g. \"Home\""},
      {.label = "City", .type = FF_TEXT, .text = tmp.city,
       .text_size = sizeof tmp.city},
      {.label = "Region", .type = FF_TEXT, .text = tmp.region,
       .text_size = sizeof tmp.region, .hint = "State or province (optional)"},
      {.label = "Country", .type = FF_TEXT, .text = tmp.country,
       .text_size = sizeof tmp.country},
      {.label = "Latitude", .type = FF_TEXT, .text = d.lat,
       .text_size = sizeof d.lat,
       .hint = "Decimal degrees, -90 (south) to 90 (north)"},
      {.label = "Longitude", .type = FF_TEXT, .text = d.lon,
       .text_size = sizeof d.lon,
       .hint = "Decimal degrees, -180 (west) to 180 (east)"},
      {.label = "Temperature", .type = FF_CHOICE, .choices = TEMP_LABELS,
       .n_choices = TEMP_COUNT, .choice = &temp},
      {.label = "Wind speed", .type = FF_CHOICE, .choices = WIND_LABELS,
       .n_choices = WIND_COUNT, .choice = &wind},
      {.label = "Precipitation", .type = FF_CHOICE, .choices = PRECIP_LABELS,
       .n_choices = PRECIP_COUNT, .choice = &precip},
      {.label = "Pressure", .type = FF_CHOICE, .choices = PRESSURE_LABELS,
       .n_choices = PRESSURE_COUNT, .choice = &pres},
      {.label = "Time format", .type = FF_CHOICE, .choices = CLOCK_LABELS,
       .n_choices = CLOCK_COUNT, .choice = &clock},
      {.label = "Default", .type = FF_BOOL, .flag = &def,
       .hint = "Used by \"wayweather use default\" and as a starting point"},
  };
  int n = (int)ARRAY_LEN(f) - (with_default ? 0 : 1);
  if (!ui_form(title, f, n, validate_location, &d))
    return false;
  tmp.units = (units_t){temp, wind, precip, pres, clock};
  tmp.is_default = with_default ? def : loc->is_default;
  /* str fields were written in place; coordinates by the validator */
  *loc = tmp;
  return true;
}

static bool units_dialog(const char *title, units_t *u) {
  int temp = u->temperature, wind = u->wind, precip = u->precipitation,
      pres = u->pressure, clock = u->clock;
  static const char *const presets[] = {"keep", "metric", "imperial"};
  int preset = 0;
  form_field f[] = {
      {.label = "Preset", .type = FF_CHOICE, .choices = presets,
       .n_choices = 3, .choice = &preset,
       .hint = "metric or imperial overrides the fields below"},
      {.label = "Temperature", .type = FF_CHOICE, .choices = TEMP_LABELS,
       .n_choices = TEMP_COUNT, .choice = &temp},
      {.label = "Wind speed", .type = FF_CHOICE, .choices = WIND_LABELS,
       .n_choices = WIND_COUNT, .choice = &wind},
      {.label = "Precipitation", .type = FF_CHOICE, .choices = PRECIP_LABELS,
       .n_choices = PRECIP_COUNT, .choice = &precip},
      {.label = "Pressure", .type = FF_CHOICE, .choices = PRESSURE_LABELS,
       .n_choices = PRESSURE_COUNT, .choice = &pres},
      {.label = "Time format", .type = FF_CHOICE, .choices = CLOCK_LABELS,
       .n_choices = CLOCK_COUNT, .choice = &clock},
  };
  if (!ui_form(title, f, (int)ARRAY_LEN(f), NULL, NULL))
    return false;
  if (preset == 1)
    units_metric(u);
  else if (preset == 2)
    units_imperial(u);
  else
    *u = (units_t){temp, wind, precip, pres, clock};
  return true;
}

static int find_duplicate(const tui_state *t, const location_t *loc,
                          int ignore) {
  for (int i = 0; i < t->cfg.n_locations; i++)
    if (i != ignore && location_same_place(&t->cfg.locations[i], loc))
      return i;
  return -1;
}

static void add_location(tui_state *t, location_t *loc) {
  int dup = find_duplicate(t, loc, -1);
  if (dup >= 0) {
    char q[256], label[FIELD_LEN];
    location_label(&t->cfg.locations[dup], label, sizeof label);
    snprintf(q, sizeof q, "This place is already saved as #%d \"%s\". "
                          "Save it again?",
             dup + 1, label);
    if (!ui_confirm("Duplicate", q, true))
      return;
  }
  int idx = config_add(&t->cfg, loc);
  if (idx < 0) {
    status(t, CP_ERROR, "You can save at most %d locations", MAX_LOCATIONS);
    return;
  }
  if (save(t)) {
    rebuild_view(t);
    select_index(t, idx);
    char label[FIELD_LEN];
    location_label(loc, label, sizeof label);
    status(t, CP_OK, "Saved \"%s\" — press Enter to show it in Waybar",
           label);
  }
}

static void action_search(tui_state *t) {
  static char query[128];
  if (!ui_input("Add location", "Search for a city (\"Paris, US\" narrows "
                                "by region or country):",
                query, sizeof query))
    return;
  if (str_empty(str_trim(query)))
    return;

  char msg[192];
  snprintf(msg, sizeof msg, "Searching for \"%s\"…", query);
  ui_busy(msg);
  geocode_result res[20];
  char err[256];
  int n = geocode_search(query, 20, res, err, sizeof err);
  if (n <= 0) {
    status(t, CP_ERROR, "%s", err);
    return;
  }

  char **items = xcalloc((size_t)n, sizeof *items);
  for (int i = 0; i < n; i++) {
    char place[512];
    location_place(&res[i].loc, place, sizeof place);
    char pop[32] = "";
    if (res[i].population >= 1000000)
      snprintf(pop, sizeof pop, " · %.1fM people", res[i].population / 1e6);
    else if (res[i].population >= 1000)
      snprintf(pop, sizeof pop, " · %ldk people", res[i].population / 1000);
    items[i] = xasprintf("%s  (%.2f, %.2f%s)", place, res[i].loc.latitude,
                         res[i].loc.longitude, pop);
  }
  char title[192];
  snprintf(title, sizeof title, "%d result%s for \"%s\"", n, n == 1 ? "" : "s",
           query);
  int pick = ui_pick(title, (const char *const *)items, n, 0);
  for (int i = 0; i < n; i++)
    free(items[i]);
  free(items);
  if (pick < 0)
    return;

  location_t loc = res[pick].loc;
  if (location_dialog("Save location", &loc, true)) {
    add_location(t, &loc);
    query[0] = '\0';
  }
}

static void action_new(tui_state *t) {
  location_t loc;
  location_init(&loc);
  loc.units = t->cfg.current.units;
  if (location_dialog("New location", &loc, true))
    add_location(t, &loc);
}

static void action_edit(tui_state *t) {
  int idx = selected_index(t);
  if (idx < 0)
    return;
  location_t loc = t->cfg.locations[idx];
  if (!location_dialog("Edit location", &loc, true))
    return;
  int dup = find_duplicate(t, &loc, idx);
  if (dup >= 0)
    status(t, CP_WARN, "Note: same place as #%d", dup + 1);
  bool was_shown = shown_index(t) == idx;
  t->cfg.locations[idx] = loc;
  config_set_default(&t->cfg,
                     loc.is_default ? idx : config_default_index(&t->cfg));
  if (was_shown)
    config_use(&t->cfg, idx);
  if (save(t) && dup < 0)
    status(t, CP_OK, "Location updated");
  rebuild_view(t);
  select_index(t, idx);
}

static void action_units(tui_state *t) {
  int idx = selected_index(t);
  if (idx < 0)
    return;
  location_t *l = &t->cfg.locations[idx];
  char title[128], label[FIELD_LEN];
  location_label(l, label, sizeof label);
  snprintf(title, sizeof title, "Units for %s", label);
  if (!units_dialog(title, &l->units))
    return;
  if (shown_index(t) == idx)
    config_use(&t->cfg, idx);
  if (save(t)) {
    char u[96];
    units_describe(&l->units, u, sizeof u);
    status(t, CP_OK, "Units: %s", u);
  }
}

static void action_delete(tui_state *t) {
  int idx = selected_index(t);
  if (idx < 0)
    return;
  char label[FIELD_LEN], q[256];
  location_label(&t->cfg.locations[idx], label, sizeof label);
  snprintf(q, sizeof q, "Delete \"%s\"? You can undo with z.", label);
  if (!ui_confirm("Delete location", q, true))
    return;
  t->undo_loc = t->cfg.locations[idx];
  t->undo_idx = idx;
  t->undo_active = shown_index(t) == idx;
  t->has_undo = true;
  config_remove(&t->cfg, idx);
  if (save(t))
    status(t, CP_WARN, "Deleted \"%s\" — press z to undo", label);
  rebuild_view(t);
}

static void action_undo(tui_state *t) {
  if (!t->has_undo) {
    status(t, CP_WARN, "Nothing to undo");
    return;
  }
  int idx = config_add(&t->cfg, &t->undo_loc);
  if (idx < 0)
    return;
  int target = t->undo_idx <= idx ? t->undo_idx : idx;
  config_move(&t->cfg, idx, target);
  if (t->undo_active && !t->cfg.geo.enabled)
    t->cfg.active = target;
  t->has_undo = false;
  if (save(t))
    status(t, CP_OK, "Restored");
  rebuild_view(t);
  select_index(t, target);
}

static void action_use(tui_state *t) {
  int idx = selected_index(t);
  if (idx < 0)
    return;
  config_use(&t->cfg, idx);
  if (save(t)) {
    char label[FIELD_LEN];
    location_label(&t->cfg.locations[idx], label, sizeof label);
    status(t, CP_OK, "Now showing \"%s\" in Waybar", label);
  }
}

static void action_default(tui_state *t) {
  int idx = selected_index(t);
  if (idx < 0)
    return;
  bool was = t->cfg.locations[idx].is_default;
  config_set_default(&t->cfg, was ? -1 : idx);
  if (save(t))
    status(t, CP_OK, was ? "Default location cleared"
                         : "Marked as the default location");
}

static void action_move(tui_state *t, int dir) {
  int idx = selected_index(t);
  if (idx < 0)
    return;
  if (t->filter_text[0]) {
    status(t, CP_WARN, "Clear the filter (Esc) to reorder");
    return;
  }
  int to = idx + dir;
  if (to < 0 || to >= t->cfg.n_locations)
    return;
  config_move(&t->cfg, idx, to);
  t->has_undo = false;
  save(t);
  rebuild_view(t);
  select_index(t, to);
}

static void action_save_current(tui_state *t) {
  location_t loc = t->cfg.current;
  if (t->cfg.geo.enabled) {
    preview p;
    preview_get(&t->cfg, &t->cfg.current, true, false, &p);
    if (!p.ok) {
      status(t, CP_WARN, "Waiting for a geolocation fix, try again shortly");
      return;
    }
    loc = p.loc;
  } else if (!app_location_set(&loc)) {
    status(t, CP_WARN, "There is no current location to save");
    return;
  }
  loc.is_default = false;
  if (location_dialog("Save current location", &loc, true))
    add_location(t, &loc);
}

/* ======================================================================
 * Current tab
 * ====================================================================== */

static void draw_current(tui_state *t) {
  int y0 = 2, h = LINES - 4;
  bool wide = COLS >= 96;
  int lw = wide ? COLS * 45 / 100 : COLS;
  const config_t *c = &t->cfg;

  draw_box(y0, 0, h, lw, "What Waybar shows", true);
  int y = y0 + 1, x = 2, w = lw - 4, bottom = y0 + h - 1;

  preview p;
  preview_get(c, &c->current, c->geo.enabled, false, &p);

  char mode[256];
  int shown = shown_index(t);
  if (c->geo.enabled)
    snprintf(mode, sizeof mode, "Geolocation (%s)",
             GEO_PROVIDER_IDS[c->geo.provider]);
  else if (shown >= 0)
    snprintf(mode, sizeof mode, "Saved location #%d", shown + 1);
  else if (app_location_set(&c->current))
    snprintf(mode, sizeof mode, "Custom location (not saved)");
  else
    snprintf(mode, sizeof mode, "Nothing configured");
  y = kv(y, x, w, "Mode", COLOR_PAIR(CP_ACCENT) | A_BOLD, mode);

  if (c->geo.enabled) {
    char place[512], coords[64];
    if (p.ok) {
      location_place(&p.loc, place, sizeof place);
      snprintf(coords, sizeof coords, "%.4f, %.4f via %s", p.loc.latitude,
               p.loc.longitude, p.source);
    } else {
      snprintf(place, sizeof place, "%s",
               p.pending ? "locating…" : "unknown");
      coords[0] = '\0';
    }
    y = kv(y, x, w, "Detected", A_BOLD, place);
    if (coords[0])
      y = kv(y, x, w, "", A_DIM, coords);
  }
  y++;
  ui_put(stdscr, y++, x, w, A_BOLD,
         c->geo.enabled ? "Fallback location" : "Location");
  if (app_location_set(&c->current)) {
    char label[FIELD_LEN], place[512], coords[64];
    location_label(&c->current, label, sizeof label);
    location_place(&c->current, place, sizeof place);
    snprintf(coords, sizeof coords, "%.4f, %.4f", c->current.latitude,
             c->current.longitude);
    if (!str_empty(c->current.name))
      y = kv(y, x, w, "Name", 0, c->current.name);
    y = kv(y, x, w, "Place", 0, place);
    y = kv(y, x, w, "Coords", 0, coords);
  } else {
    y = kv(y, x, w, "", A_DIM, "not set — press e or pick a saved location");
  }
  char units[96], ubuf[128];
  units_describe(&c->current.units, units, sizeof units);
  snprintf(ubuf, sizeof ubuf, "%s (%s)", units,
           units_preset_name(&c->current.units));
  y = kv(y, x, w, "Units", 0, ubuf);

  y++;
  ui_put(stdscr, y++, x, w, A_BOLD, "Geolocation");
  y = kv(y, x, w, "Enabled", c->geo.enabled ? COLOR_PAIR(CP_OK) | A_BOLD : 0,
         c->geo.enabled ? "yes — overrides the location above" : "no");
  char prov[128];
  snprintf(prov, sizeof prov, "%s%s", GEO_PROVIDER_IDS[c->geo.provider],
           c->geo.provider == GEO_AUTO
               ? (geolocate_has_geoclue() ? " (gpsd → GeoClue → IP)"
                                          : " (gpsd → IP)")
               : "");
  y = kv(y, x, w, "Provider", 0, prov);

  /* Bar preview */
  if (y + 2 < bottom) {
    y++;
    ui_put(stdscr, y++, x, w, A_BOLD, "Bar text");
    if (p.ok) {
      render_ctx ctx = {c, &p.loc, &p.w, p.source, c->display.icons};
      char *text = render_template(&ctx, c->display.text);
      ui_putf(stdscr, y++, x, w, COLOR_PAIR(CP_ACCENT) | A_BOLD, " %s ", text);
      free(text);
    } else {
      ui_put(stdscr, y++, x, w, A_DIM, p.pending ? "…" : "unavailable");
    }
  }

  if (wide) {
    draw_box(y0, lw, h, COLS - lw, "Weather", false);
    draw_weather(t, &p, y0 + 1, lw + 2, COLS - lw - 4, bottom);
  } else if (y + 3 < bottom) {
    ui_rule(stdscr, y++, 0, lw, COLOR_PAIR(CP_ACCENT));
    draw_weather(t, &p, y, x, w, bottom);
  }
}

static void action_toggle_geo(tui_state *t) {
  t->cfg.geo.enabled = !t->cfg.geo.enabled;
  if (t->cfg.geo.enabled)
    t->cfg.active = -1;
  if (save(t))
    status(t, CP_OK, t->cfg.geo.enabled
                         ? "Geolocation on: Waybar follows your position"
                         : "Geolocation off: showing the configured location");
}

static void action_cycle_provider(tui_state *t) {
  int p = (t->cfg.geo.provider + 1) % GEO_PROVIDER_COUNT;
  if (p == GEO_GEOCLUE && !geolocate_has_geoclue())
    p = (p + 1) % GEO_PROVIDER_COUNT;
  t->cfg.geo.provider = p;
  preview_drop_geo();
  if (save(t))
    status(t, CP_OK, "Geolocation provider: %s", GEO_PROVIDER_IDS[p]);
}

static void action_edit_current(tui_state *t) {
  location_t loc = t->cfg.current;
  if (!location_dialog("Current location", &loc, false))
    return;
  t->cfg.current = loc;
  t->cfg.geo.enabled = false;
  t->cfg.active = config_match_current(&t->cfg);
  if (save(t))
    status(t, CP_OK, "Current location updated, geolocation off");
}

static void action_current_units(tui_state *t) {
  if (!units_dialog("Units for the current location", &t->cfg.current.units))
    return;
  int shown = shown_index(t);
  if (shown >= 0)
    t->cfg.locations[shown].units = t->cfg.current.units;
  preview_drop_geo();
  if (save(t)) {
    char u[96];
    units_describe(&t->cfg.current.units, u, sizeof u);
    status(t, CP_OK, "Units: %s", u);
  }
}

static void action_refresh(tui_state *t) {
  preview p;
  if (t->tab == TAB_LOCATIONS) {
    int idx = selected_index(t);
    if (idx < 0)
      return;
    preview_get(&t->cfg, &t->cfg.locations[idx], false, true, &p);
  } else {
    preview_get(&t->cfg, &t->cfg.current, t->cfg.geo.enabled, true, &p);
  }
  status(t, CP_ACCENT, "Refreshing…");
}

/* ======================================================================
 * Settings tab
 * ====================================================================== */

typedef enum { S_BOOL, S_INT, S_STR, S_ENUM } setting_type;

typedef struct {
  const char *section;
  const char *label;
  const char *desc;
  setting_type type;
  int min, max, step;
} setting_def;

enum {
  SET_ICONS,
  SET_TEXT,
  SET_TOOLTIP,
  SET_TOOLTIP_TITLE,
  SET_HOURS,
  SET_DAYS,
  SET_DECIMALS,
  SET_REFRESH,
  SET_CACHE,
  SET_GEO,
  SET_GEO_PROVIDER,
  SET_GEO_CYCLE,
  SET_GEO_REVERSE,
  SET_GEO_TIMEOUT,
  SET_GEO_CACHE,
  SET_GPSD_HOST,
  SET_GPSD_PORT,
  SET_COUNT
};

static const setting_def SETTINGS[SET_COUNT] = {
    [SET_ICONS] = {"Waybar", "Weather icons",
                   "Nerd Font glyphs in the bar, tooltip and TUI", S_BOOL},
    [SET_TEXT] = {"Waybar", "Bar text",
                  "Template for the bar. See \"wayweather help format\", "
                  "e.g. {icon} {temp}{tunit} {wind_arrow}",
                  S_STR},
    [SET_TOOLTIP] = {"Waybar", "Tooltip", "Show the detailed tooltip",
                     S_BOOL},
    [SET_TOOLTIP_TITLE] = {"Waybar", "Tooltip title",
                           "Template for the first tooltip line, "
                           "e.g. {name} or {place}",
                           S_STR},
    [SET_HOURS] = {"Waybar", "Forecast hours",
                   "Hourly rows in the tooltip and \"wayweather print\"",
                   S_INT, 0, 24, 1},
    [SET_DAYS] = {"Waybar", "Forecast days",
                  "Daily rows in the tooltip and \"wayweather print\"", S_INT,
                  0, 7, 1},
    [SET_DECIMALS] = {"Waybar", "Temperature decimals",
                      "Digits after the decimal point", S_INT, 0, 2, 1},
    [SET_REFRESH] = {"Updates", "Refresh interval",
                     "Seconds between daemon updates", S_INT, 60, 86400, 60},
    [SET_CACHE] = {"Updates", "Weather cache",
                   "Seconds a forecast is reused by \"wayweather get\" "
                   "(0 disables)",
                   S_INT, 0, 86400, 60},
    [SET_GEO] = {"Geolocation", "Use geolocation",
                 "Let GPS / IP position override the current location",
                 S_BOOL},
    [SET_GEO_PROVIDER] = {"Geolocation", "Provider",
                          "auto tries gpsd, GeoClue (if built in), then IP",
                          S_ENUM},
    [SET_GEO_CYCLE] = {"Geolocation", "Include in next/prev",
                       "\"wayweather next\" also steps through geolocation",
                       S_BOOL},
    [SET_GEO_REVERSE] = {"Geolocation", "Look up place names",
                         "Name GPS fixes using OpenStreetMap Nominatim",
                         S_BOOL},
    [SET_GEO_TIMEOUT] = {"Geolocation", "Timeout",
                         "Seconds to wait for each provider", S_INT, 1, 60,
                         1},
    [SET_GEO_CACHE] = {"Geolocation", "Position cache",
                       "Seconds a position fix is reused", S_INT, 0, 86400,
                       60},
    [SET_GPSD_HOST] = {"Geolocation", "gpsd host", "Where gpsd listens",
                       S_STR},
    [SET_GPSD_PORT] = {"Geolocation", "gpsd port", "Default 2947", S_INT, 1,
                       65535, 1},
};

static void *setting_ptr(config_t *c, int id, size_t *size) {
  *size = 0;
  switch (id) {
  case SET_ICONS:
    return &c->display.icons;
  case SET_TEXT:
    *size = sizeof c->display.text;
    return c->display.text;
  case SET_TOOLTIP:
    return &c->display.tooltip;
  case SET_TOOLTIP_TITLE:
    *size = sizeof c->display.tooltip_title;
    return c->display.tooltip_title;
  case SET_HOURS:
    return &c->display.hours;
  case SET_DAYS:
    return &c->display.days;
  case SET_DECIMALS:
    return &c->display.decimals;
  case SET_REFRESH:
    return &c->refresh_interval;
  case SET_CACHE:
    return &c->cache_ttl;
  case SET_GEO:
    return &c->geo.enabled;
  case SET_GEO_PROVIDER:
    return &c->geo.provider;
  case SET_GEO_CYCLE:
    return &c->geo.in_cycle;
  case SET_GEO_REVERSE:
    return &c->geo.reverse_geocode;
  case SET_GEO_TIMEOUT:
    return &c->geo.timeout;
  case SET_GEO_CACHE:
    return &c->geo.cache_ttl;
  case SET_GPSD_HOST:
    *size = sizeof c->geo.gpsd_host;
    return c->geo.gpsd_host;
  case SET_GPSD_PORT:
    return &c->geo.gpsd_port;
  }
  return NULL;
}

static void setting_value(config_t *c, int id, char *buf, size_t size) {
  size_t sz;
  void *p = setting_ptr(c, id, &sz);
  const setting_def *d = &SETTINGS[id];
  switch (d->type) {
  case S_BOOL:
    snprintf(buf, size, "%s", *(bool *)p ? "[x] on" : "[ ] off");
    break;
  case S_INT: {
    int v = *(int *)p;
    if ((id == SET_REFRESH || id == SET_CACHE || id == SET_GEO_CACHE) &&
        v >= 60 && v % 60 == 0)
      snprintf(buf, size, "%d s (%d min)", v, v / 60);
    else if (id == SET_GEO_TIMEOUT || id == SET_CACHE || id == SET_GEO_CACHE)
      snprintf(buf, size, "%d s", v);
    else
      snprintf(buf, size, "%d", v);
    break;
  }
  case S_STR:
    snprintf(buf, size, "%s", (char *)p);
    break;
  case S_ENUM: {
    int v = *(int *)p;
    snprintf(buf, size, "‹ %s ›", GEO_PROVIDER_IDS[v]);
    break;
  }
  }
}

static void draw_settings(tui_state *t) {
  int y0 = 2, h = LINES - 4;
  draw_box(y0, 0, h, COLS, "Settings", true);
  int x = 2, w = COLS - 4;
  int labelw = 24;
  int footer = 4; /* description + preview */
  int rows = h - 2 - footer;

  /* Build visual rows: section headers + settings */
  int map[SET_COUNT * 2], kind[SET_COUNT * 2], n = 0, sel_row = 0;
  const char *section = NULL;
  for (int i = 0; i < SET_COUNT; i++) {
    if (!section || strcmp(section, SETTINGS[i].section) != 0) {
      section = SETTINGS[i].section;
      if (n > 0) {
        kind[n] = 2; /* spacer */
        map[n++] = -1;
      }
      kind[n] = 1;
      map[n++] = i;
    }
    if (i == t->set_sel)
      sel_row = n;
    kind[n] = 0;
    map[n++] = i;
  }
  if (sel_row < t->set_top + 1)
    t->set_top = sel_row - 1 < 0 ? 0 : sel_row - 1;
  if (sel_row >= t->set_top + rows)
    t->set_top = sel_row - rows + 1;

  for (int r = 0; r < rows && t->set_top + r < n; r++) {
    int vr = t->set_top + r;
    int y = y0 + 1 + r;
    if (kind[vr] == 2)
      continue;
    const setting_def *d = &SETTINGS[map[vr]];
    if (kind[vr] == 1) {
      ui_put(stdscr, y, x, w, COLOR_PAIR(CP_ACCENT) | A_BOLD, d->section);
      continue;
    }
    bool selected = map[vr] == t->set_sel;
    attr_t a = selected ? COLOR_PAIR(CP_SELECTED) : 0;
    ui_fill(stdscr, y, 1, COLS - 2, a);
    ui_put(stdscr, y, x + 2, labelw, a, d->label);
    char val[512];
    setting_value(&t->cfg, map[vr], val, sizeof val);
    ui_put(stdscr, y, x + 2 + labelw, w - labelw - 2, a | A_BOLD, val);
  }

  /* Description and live preview */
  int fy = y0 + h - 1 - footer;
  ui_rule(stdscr, fy, 0, COLS, COLOR_PAIR(CP_ACCENT));
  const setting_def *d = &SETTINGS[t->set_sel];
  ui_put(stdscr, fy + 1, x, w, A_DIM, d->desc);

  preview p;
  preview_get(&t->cfg, &t->cfg.current, t->cfg.geo.enabled, false, &p);
  int px = x + ui_put(stdscr, fy + 2, x, w, A_BOLD, "Bar preview: ");
  if (p.ok) {
    render_ctx ctx = {&t->cfg, &p.loc, &p.w, p.source, t->cfg.display.icons};
    char *text = render_template(&ctx, t->cfg.display.text);
    ui_putf(stdscr, fy + 2, px, x + w - px, COLOR_PAIR(CP_ACCENT) | A_BOLD,
            " %s ", text);
    free(text);
    char *title = render_template(&ctx, t->cfg.display.tooltip_title);
    int tx = x + ui_put(stdscr, fy + 3, x, w, A_BOLD, "Tooltip title: ");
    ui_put(stdscr, fy + 3, tx, x + w - tx, 0, title);
    free(title);
  } else {
    ui_put(stdscr, fy + 2, px, x + w - px, A_DIM,
           p.pending ? "loading…" : "no weather data");
  }
  ui_put_right(stdscr, y0 + h - 1, COLS - 3, A_DIM, config_path());
}

static void setting_changed(tui_state *t, int id) {
  if (id == SET_GEO && t->cfg.geo.enabled)
    t->cfg.active = -1;
  if (id >= SET_GEO_PROVIDER && id <= SET_GPSD_PORT)
    preview_drop_geo();
  if (save(t)) {
    char val[512];
    setting_value(&t->cfg, id, val, sizeof val);
    status(t, CP_OK, "%s: %s", SETTINGS[id].label, val);
  }
}

static void setting_adjust(tui_state *t, int dir) {
  int id = t->set_sel;
  const setting_def *d = &SETTINGS[id];
  size_t sz;
  void *p = setting_ptr(&t->cfg, id, &sz);
  switch (d->type) {
  case S_BOOL:
    *(bool *)p = !*(bool *)p;
    break;
  case S_INT: {
    int v = *(int *)p + dir * d->step;
    *(int *)p = CLAMP(v, d->min, d->max);
    break;
  }
  case S_ENUM: {
    int v = *(int *)p;
    do
      v = (v + dir + GEO_PROVIDER_COUNT) % GEO_PROVIDER_COUNT;
    while (v == GEO_GEOCLUE && !geolocate_has_geoclue());
    *(int *)p = v;
    break;
  }
  case S_STR:
    return;
  }
  setting_changed(t, id);
}

static void setting_activate(tui_state *t) {
  int id = t->set_sel;
  const setting_def *d = &SETTINGS[id];
  size_t sz;
  void *p = setting_ptr(&t->cfg, id, &sz);
  if (d->type == S_BOOL || d->type == S_ENUM) {
    setting_adjust(t, 1);
    return;
  }
  char buf[FORMAT_LEN];
  if (d->type == S_STR)
    str_copy(buf, p, sizeof buf);
  else
    snprintf(buf, sizeof buf, "%d", *(int *)p);
  char prompt[256];
  if (d->type == S_INT)
    snprintf(prompt, sizeof prompt, "%s (%d – %d):", d->label, d->min,
             d->max);
  else
    snprintf(prompt, sizeof prompt, "%s:", d->label);
  if (!ui_input(d->label, prompt, buf, sizeof buf))
    return;
  if (d->type == S_STR) {
    if (id == SET_GPSD_HOST && str_empty(str_trim(buf))) {
      status(t, CP_ERROR, "The host can't be empty");
      return;
    }
    str_copy(p, buf, sz);
  } else {
    long v;
    if (!parse_long(str_trim(buf), &v) || v < d->min || v > d->max) {
      status(t, CP_ERROR, "Enter a whole number between %d and %d", d->min,
             d->max);
      return;
    }
    *(int *)p = (int)v;
  }
  setting_changed(t, id);
}

static void setting_reset(tui_state *t) {
  config_t def;
  config_defaults(&def);
  size_t sz;
  void *dst = setting_ptr(&t->cfg, t->set_sel, &sz);
  void *src = setting_ptr(&def, t->set_sel, &sz);
  switch (SETTINGS[t->set_sel].type) {
  case S_BOOL:
    *(bool *)dst = *(bool *)src;
    break;
  case S_INT:
  case S_ENUM:
    *(int *)dst = *(int *)src;
    break;
  case S_STR:
    str_copy(dst, src, sz);
    break;
  }
  setting_changed(t, t->set_sel);
}

/* ======================================================================
 * Help
 * ====================================================================== */

static const char HELP_TEXT[] =
    "Everywhere\n"
    "  1 2 3 / Tab       switch between Locations, Current and Settings\n"
    "  g                 toggle geolocation (GPS / IP)\n"
    "  r                 refresh the weather preview\n"
    "  R                 reload the config file from disk\n"
    "  ?                 this help\n"
    "  q                 quit (changes are saved immediately)\n"
    "\n"
    "Locations\n"
    "  ↑ ↓ j k PgUp PgDn move the selection (mouse wheel works too)\n"
    "  Enter / Space     show the location in Waybar (double click)\n"
    "  a                 search a city and save it\n"
    "  n                 add a location by coordinates\n"
    "  e                 edit name, place, coordinates and units\n"
    "  u                 change the units of the location\n"
    "  d / Delete        delete (asks first)\n"
    "  z                 undo the last deletion\n"
    "  * / D             make it the default (again to clear)\n"
    "  J K / Shift-↑↓    move the location up or down\n"
    "  /                 filter the list; Esc clears the filter\n"
    "  s                 save the current location as a new entry\n"
    "\n"
    "Current\n"
    "  e                 edit the current location (turns geolocation off)\n"
    "  u                 change the units of the current location\n"
    "  p                 cycle the geolocation provider\n"
    "  s                 save the current/detected location\n"
    "\n"
    "Settings\n"
    "  ↑ ↓               select a setting\n"
    "  ← →               change values / toggle\n"
    "  Enter             edit text and numbers, toggle switches\n"
    "  x                 reset the setting to its default\n"
    "\n"
    "Forms\n"
    "  Tab / ↑ ↓         next / previous field\n"
    "  ← →               change a choice; move the cursor in text\n"
    "  Ctrl-U Ctrl-K Ctrl-W   clear before / after cursor, delete word\n"
    "  Enter / F2        save            Esc  cancel\n"
    "\n"
    "Waybar picks up every change instantly when it runs\n"
    "\"wayweather daemon\". Weather data by Open-Meteo.com.\n";

/* ======================================================================
 * Input
 * ====================================================================== */

static void move_sel(tui_state *t, int delta) {
  if (t->tab == TAB_LOCATIONS) {
    if (t->n_view == 0)
      return;
    t->sel = CLAMP(t->sel + delta, 0, t->n_view - 1);
  } else if (t->tab == TAB_SETTINGS) {
    t->set_sel = CLAMP(t->set_sel + delta, 0, SET_COUNT - 1);
  }
}

static void handle_mouse(tui_state *t) {
  MEVENT ev;
  if (getmouse(&ev) != OK)
    return;
  if (ev.bstate & BUTTON4_PRESSED) {
    move_sel(t, -1);
    return;
  }
  if (ev.bstate & BUTTON5_PRESSED) {
    move_sel(t, 1);
    return;
  }
  if (ev.y == 1) {
    for (int i = 0; i < TAB_COUNT; i++)
      if (ev.x >= t->tab_x[i] && ev.x < t->tab_x[i + 1])
        t->tab = i;
    return;
  }
  if (t->tab == TAB_LOCATIONS && ev.y >= t->list_first_row &&
      ev.y < t->list_first_row + t->list_rows && ev.x >= t->list_x0 &&
      ev.x < t->list_x1) {
    int pos = t->top + ev.y - t->list_first_row;
    if (pos < t->n_view) {
      t->sel = pos;
      if (ev.bstate & BUTTON1_DOUBLE_CLICKED)
        action_use(t);
    }
  }
}

/* Keys available on every tab; returns true if handled */
static bool handle_global(tui_state *t, int rc, wint_t ch) {
  if (rc == KEY_CODE_YES) {
    switch (ch) {
    case KEY_RESIZE:
      return true;
    case KEY_MOUSE:
      handle_mouse(t);
      return true;
    case KEY_BTAB:
      t->tab = (t->tab + TAB_COUNT - 1) % TAB_COUNT;
      return true;
    case KEY_F(1):
      ui_message("Key bindings", HELP_TEXT);
      return true;
    }
    return false;
  }
  switch (ch) {
  case 'q':
    t->quit = true;
    return true;
  case '\t':
    t->tab = (t->tab + 1) % TAB_COUNT;
    return true;
  case '1':
  case '2':
  case '3':
    t->tab = (tab_id)(ch - '1');
    return true;
  case '?':
    ui_message("Key bindings", HELP_TEXT);
    return true;
  case 'g':
    action_toggle_geo(t);
    return true;
  case 'r':
    action_refresh(t);
    return true;
  case 'R':
    t->cfg_mtime = (struct timespec){0, 0};
    t->last_check = 0;
    reload_if_changed(t);
    return true;
  }
  return false;
}

static void handle_locations(tui_state *t, int rc, wint_t ch) {
  int page = t->list_rows > 1 ? t->list_rows - 1 : 1;
  if (rc == KEY_CODE_YES) {
    switch (ch) {
    case KEY_UP:
      move_sel(t, -1);
      break;
    case KEY_DOWN:
      move_sel(t, 1);
      break;
    case KEY_PPAGE:
      move_sel(t, -page);
      break;
    case KEY_NPAGE:
      move_sel(t, page);
      break;
    case KEY_HOME:
      t->sel = 0;
      break;
    case KEY_END:
      t->sel = t->n_view ? t->n_view - 1 : 0;
      break;
    case KEY_ENTER:
      action_use(t);
      break;
    case KEY_DC:
      action_delete(t);
      break;
    case KEY_SR:
      action_move(t, -1);
      break;
    case KEY_SF:
      action_move(t, 1);
      break;
    }
    return;
  }
  switch (ch) {
  case 'k':
    move_sel(t, -1);
    break;
  case 'j':
    move_sel(t, 1);
    break;
  case 'K':
    action_move(t, -1);
    break;
  case 'J':
    action_move(t, 1);
    break;
  case ' ':
    action_use(t);
    break;
  case 'a':
    action_search(t);
    break;
  case 'n':
    action_new(t);
    break;
  case 'e':
    action_edit(t);
    break;
  case 'u':
    action_units(t);
    break;
  case 'd':
  case 'x':
    action_delete(t);
    break;
  case 'z':
    action_undo(t);
    break;
  case '*':
  case 'D':
    action_default(t);
    break;
  case 's':
    action_save_current(t);
    break;
  case '/':
    t->filtering = true;
    le_set(&t->filter, t->filter_text);
    break;
  case KEY_ESC:
    if (t->filter_text[0]) {
      t->filter_text[0] = '\0';
      rebuild_view(t);
    }
    break;
  }
}

static void handle_filter(tui_state *t, int rc, wint_t ch) {
  if (rc == KEY_CODE_YES && (ch == KEY_ENTER || ch == KEY_UP ||
                             ch == KEY_DOWN)) {
    t->filtering = false;
    if (ch != KEY_ENTER)
      move_sel(t, ch == KEY_UP ? -1 : 1);
    return;
  }
  if (rc == OK && ch == KEY_ESC) {
    t->filtering = false;
    t->filter_text[0] = '\0';
    rebuild_view(t);
    return;
  }
  if (le_key(&t->filter, rc, ch)) {
    le_get(&t->filter, t->filter_text, sizeof t->filter_text);
    t->sel = 0;
    rebuild_view(t);
  }
}

static void handle_current(tui_state *t, int rc, wint_t ch) {
  if (rc != OK)
    return;
  switch (ch) {
  case 'e':
    action_edit_current(t);
    break;
  case 'u':
    action_current_units(t);
    break;
  case 'p':
    action_cycle_provider(t);
    break;
  case 's':
    action_save_current(t);
    break;
  }
}

static void handle_settings(tui_state *t, int rc, wint_t ch) {
  if (rc == KEY_CODE_YES) {
    switch (ch) {
    case KEY_UP:
      move_sel(t, -1);
      break;
    case KEY_DOWN:
      move_sel(t, 1);
      break;
    case KEY_HOME:
      t->set_sel = 0;
      break;
    case KEY_END:
      t->set_sel = SET_COUNT - 1;
      break;
    case KEY_LEFT:
      setting_adjust(t, -1);
      break;
    case KEY_RIGHT:
      setting_adjust(t, 1);
      break;
    case KEY_ENTER:
      setting_activate(t);
      break;
    }
    return;
  }
  switch (ch) {
  case 'k':
    move_sel(t, -1);
    break;
  case 'j':
    move_sel(t, 1);
    break;
  case 'h':
  case '-':
    setting_adjust(t, -1);
    break;
  case 'l':
  case '+':
    setting_adjust(t, 1);
    break;
  case ' ':
    setting_activate(t);
    break;
  case 'x':
    setting_reset(t);
    break;
  }
}

/* ======================================================================
 * Main loop
 * ====================================================================== */

static const hint HINTS_LOCATIONS[] = {
    {"⏎", "show"}, {"a", "add"},     {"e", "edit"},    {"d", "delete"},
    {"*", "default"}, {"u", "units"}, {"J/K", "move"}, {"/", "filter"},
    {"g", "geo"},  {"?", "help"},    {"q", "quit"}};
static const hint HINTS_FILTER[] = {
    {"type", "to filter"}, {"⏎", "done"}, {"Esc", "clear"}};
static const hint HINTS_CURRENT[] = {
    {"g", "toggle geolocation"}, {"p", "provider"}, {"e", "edit"},
    {"u", "units"}, {"s", "save as location"}, {"r", "refresh"},
    {"?", "help"},  {"q", "quit"}};
static const hint HINTS_SETTINGS[] = {
    {"↑↓", "select"}, {"←→", "change"}, {"⏎", "edit"},
    {"x", "reset"},   {"?", "help"},    {"q", "quit"}};

static void draw(tui_state *t) {
  erase();
  if (COLS < MIN_COLS || LINES < MIN_LINES) {
    char msg[96];
    snprintf(msg, sizeof msg, "Terminal too small (%dx%d, need %dx%d)", COLS,
             LINES, MIN_COLS, MIN_LINES);
    ui_put_center(stdscr, LINES / 2, COLS, A_BOLD, msg);
    ui_put_center(stdscr, LINES / 2 + 1, COLS, A_DIM, "q quits");
    refresh();
    return;
  }
  draw_header(t);
  switch (t->tab) {
  case TAB_LOCATIONS:
    draw_locations(t);
    if (t->filtering)
      draw_hints(HINTS_FILTER, ARRAY_LEN(HINTS_FILTER));
    else
      draw_hints(HINTS_LOCATIONS, ARRAY_LEN(HINTS_LOCATIONS));
    break;
  case TAB_CURRENT:
    draw_current(t);
    draw_hints(HINTS_CURRENT, ARRAY_LEN(HINTS_CURRENT));
    break;
  case TAB_SETTINGS:
    draw_settings(t);
    draw_hints(HINTS_SETTINGS, ARRAY_LEN(HINTS_SETTINGS));
    break;
  default:
    break;
  }
  draw_status(t);

  if (t->tab == TAB_LOCATIONS && t->filtering) {
    int inner = (COLS >= 100 ? COLS * 40 / 100 : COLS >= 76 ? 34 : COLS) - 2;
    curs_set(1);
    le_draw(&t->filter, stdscr, 3, 4, inner - 4, true);
  } else {
    curs_set(0);
  }
  refresh();
}

int tui_run(void) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    log_error("The TUI needs a terminal");
    return 1;
  }
  static tui_state t; /* large: keep it off the stack */
  memset(&t, 0, sizeof t);
  if (!config_load(&t.cfg))
    return 1;
  t.cfg_mtime = file_mtime(config_path());
  t.last_check = now_ms();
  t.tab = t.cfg.n_locations == 0 && !t.cfg.geo.enabled &&
                  app_location_set(&t.cfg.current)
              ? TAB_CURRENT
              : TAB_LOCATIONS;
  rebuild_view(&t);
  int shown = shown_index(&t);
  if (shown >= 0)
    select_index(&t, shown);
  else if (config_default_index(&t.cfg) >= 0)
    select_index(&t, config_default_index(&t.cfg));

  log_capture(true);
  ui_init();
  preview_start(&t.cfg);
  timeout(250);
  if (t.cfg.n_locations == 0 && !app_location_set(&t.cfg.current))
    status(&t, CP_ACCENT, "Welcome! Press a to add your first location");

  while (!t.quit) {
    reload_if_changed(&t);
    preview_take_update();
    draw(&t);
    wint_t ch;
    int rc = ui_getkey(stdscr, &ch);
    if (rc == ERR)
      continue;
    if (COLS < MIN_COLS || LINES < MIN_LINES) {
      if (rc == OK && ch == 'q')
        t.quit = true;
      continue;
    }
    if (t.tab == TAB_LOCATIONS && t.filtering) {
      handle_filter(&t, rc, ch);
      continue;
    }
    if (handle_global(&t, rc, ch))
      continue;
    switch (t.tab) {
    case TAB_LOCATIONS:
      handle_locations(&t, rc, ch);
      break;
    case TAB_CURRENT:
      handle_current(&t, rc, ch);
      break;
    case TAB_SETTINGS:
      handle_settings(&t, rc, ch);
      break;
    default:
      break;
    }
  }

  ui_shutdown();
  log_capture(false);
  bool clean = preview_stop();
  config_free(&t.cfg);
  if (!clean) {
    /* A request is still in flight; everything is saved, don't wait */
    fflush(NULL);
    _exit(0);
  }
  return 0;
}
