#ifndef WW_RENDER_H_
#define WW_RENDER_H_

#include "config.h"
#include "util.h"
#include "weather.h"
#include <stdbool.h>
#include <stdio.h>

typedef struct {
  const config_t *cfg;
  const location_t *loc; /* the location the weather belongs to */
  const weather_t *w;
  const char *source; /* "config", "saved", "gpsd", "ip (cached)", ... */
  bool icons;
} render_ctx;

/* Expands {placeholders} in `tmpl` (caller frees). See `wayweather help
 * format` for the list. Unknown placeholders are kept verbatim. */
char *render_template(const render_ctx *ctx, const char *tmpl);

/* One line of Waybar JSON (no trailing newline, caller frees) */
char *render_waybar(const render_ctx *ctx);
char *render_waybar_error(const char *text, const char *tooltip);

/* Human readable report for terminals */
void render_print(FILE *out, const render_ctx *ctx, bool color);

/* Formatting helpers shared with the TUI */
void fmt_number(char *buf, size_t size, double v, int decimals);
void fmt_clock(char *buf, size_t size, const char *iso, clock_format clock);
/* "Today", "Tomorrow" or abbreviated weekday for a daily entry */
void fmt_day(char *buf, size_t size, const weather_day *days, int idx);
/* Appends `s` padded with spaces to `cols` display columns */
void sb_append_padded(strbuf *sb, const char *s, int cols);

#endif // !WW_RENDER_H_
