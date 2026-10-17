#define _GNU_SOURCE
#include "render.h"
#include "units.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- Formatting helpers ---------- */

void fmt_number(char *buf, size_t size, double v, int decimals) {
  if (isnan(v)) {
    snprintf(buf, size, "--");
    return;
  }
  /* Avoid printing "-0" for values that round to zero */
  double scale = pow(10, decimals);
  if (round(v * scale) == 0)
    v = 0;
  snprintf(buf, size, "%.*f", decimals, v);
}

void fmt_clock(char *buf, size_t size, const char *iso, clock_format clock) {
  int h = 0, m = 0;
  const char *t = strchr(iso, 'T');
  if (!t || sscanf(t + 1, "%d:%d", &h, &m) != 2) {
    snprintf(buf, size, "--:--");
    return;
  }
  if (clock == CLOCK_12H) {
    int h12 = h % 12 == 0 ? 12 : h % 12;
    snprintf(buf, size, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
  } else {
    snprintf(buf, size, "%02d:%02d", h, m);
  }
}

void fmt_day(char *buf, size_t size, const weather_day *days, int idx) {
  if (idx == 0) {
    snprintf(buf, size, "Today");
    return;
  }
  if (idx == 1) {
    snprintf(buf, size, "Tomorrow");
    return;
  }
  struct tm tm = {0};
  if (sscanf(days[idx].date, "%d-%d-%d", &tm.tm_year, &tm.tm_mon,
             &tm.tm_mday) != 3) {
    snprintf(buf, size, "%s", days[idx].date);
    return;
  }
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  mktime(&tm);
  strftime(buf, size, "%a %d", &tm);
}

void sb_append_padded(strbuf *sb, const char *s, int cols) {
  sb_append(sb, s);
  for (int w = utf8_width(s); w < cols; w++)
    sb_appendc(sb, ' ');
}

static int precip_decimals(const units_t *u) {
  return u->precipitation == PRECIP_INCH ? 2 : 1;
}

static int pressure_decimals(const units_t *u) {
  return u->pressure == PRESSURE_INHG || u->pressure == PRESSURE_KPA ? 2 : 0;
}

static void fmt_visibility(char *buf, size_t size, double meters,
                           const units_t *u) {
  if (isnan(meters)) {
    snprintf(buf, size, "--");
    return;
  }
  if (u->precipitation == PRECIP_INCH)
    snprintf(buf, size, "%.1f mi", meters / 1609.344);
  else
    snprintf(buf, size, "%.1f km", meters / 1000.0);
}

static const char *temp_class(const weather_t *w) {
  double c = temp_to_celsius(w->temperature, w->units.temperature);
  if (c >= 30)
    return "hot";
  if (c >= 20)
    return "warm";
  if (c >= 10)
    return "mild";
  if (c >= 0)
    return "cool";
  return "cold";
}

/* ---------- Template expansion ---------- */

static bool lookup(const render_ctx *ctx, const char *key, strbuf *out) {
  const weather_t *w = ctx->w;
  const location_t *loc = ctx->loc;
  const units_t *u = &w->units;
  int dec = ctx->cfg->display.decimals;
  const weather_day *today = w->n_days > 0 ? &w->days[0] : NULL;
  char b[160];

#define IS(k) (strcmp(key, k) == 0)
#define NUM(v, d) (fmt_number(b, sizeof b, (v), (d)), sb_append(out, b), true)
  if (IS("icon"))
    return sb_append(out, ctx->icons ? wmo_icon(w->code, w->is_day) : ""),
           true;
  if (IS("temp"))
    return NUM(w->temperature, dec);
  if (IS("feels"))
    return NUM(w->apparent_temperature, dec);
  if (IS("tunit"))
    return sb_append(out, TEMP_LABELS[u->temperature]), true;
  if (IS("desc"))
    return sb_append(out, wmo_description(w->code)), true;
  if (IS("condition"))
    return sb_append(out, wmo_class(w->code)), true;
  if (IS("humidity"))
    return NUM(w->humidity, 0);
  if (IS("dew"))
    return NUM(w->dew_point, dec);
  if (IS("precip"))
    return NUM(w->precipitation, precip_decimals(u));
  if (IS("punit"))
    return sb_append(out, PRECIP_LABELS[u->precipitation]), true;
  if (IS("precip_prob"))
    return NUM(w->n_hours ? w->hours[0].precip_probability : NAN, 0);
  if (IS("cloud"))
    return NUM(w->cloud_cover, 0);
  if (IS("wind"))
    return NUM(w->wind_speed, 1);
  if (IS("gust"))
    return NUM(w->wind_gusts, 1);
  if (IS("wunit"))
    return sb_append(out, WIND_LABELS[u->wind]), true;
  if (IS("wind_dir"))
    return sb_append(out, wind_compass(w->wind_direction)), true;
  if (IS("wind_arrow"))
    return sb_append(out, wind_arrow(w->wind_direction)), true;
  if (IS("wind_deg"))
    return NUM(w->wind_direction, 0);
  if (IS("pressure"))
    return NUM(pressure_from_hpa(w->pressure_hpa, u->pressure),
               pressure_decimals(u));
  if (IS("presunit"))
    return sb_append(out, PRESSURE_LABELS[u->pressure]), true;
  if (IS("uv"))
    return NUM(w->uv_index, 1);
  if (IS("visibility"))
    return fmt_visibility(b, sizeof b, w->visibility_m, u), sb_append(out, b),
           true;
  if (IS("tmax"))
    return NUM(today ? today->temp_max : NAN, dec);
  if (IS("tmin"))
    return NUM(today ? today->temp_min : NAN, dec);
  if (IS("sunrise"))
    return fmt_clock(b, sizeof b, today ? today->sunrise : "", u->clock),
           sb_append(out, b), true;
  if (IS("sunset"))
    return fmt_clock(b, sizeof b, today ? today->sunset : "", u->clock),
           sb_append(out, b), true;
  if (IS("time") || IS("updated"))
    return fmt_clock(b, sizeof b, w->time, u->clock), sb_append(out, b), true;
  if (IS("date"))
    return sb_appendn(out, w->time, strnlen(w->time, 10)), true;
  if (IS("name"))
    return location_label(loc, b, sizeof b), sb_append(out, b), true;
  if (IS("city"))
    return sb_append(out, loc->city), true;
  if (IS("region"))
    return sb_append(out, loc->region), true;
  if (IS("country"))
    return sb_append(out, loc->country), true;
  if (IS("place"))
    return location_place(loc, b, sizeof b), sb_append(out, b), true;
  if (IS("lat"))
    return NUM(loc->latitude, 4);
  if (IS("lon"))
    return NUM(loc->longitude, 4);
  if (IS("source"))
    return sb_append(out, ctx->source ? ctx->source : ""), true;
#undef NUM
#undef IS
  return false;
}

char *render_template(const render_ctx *ctx, const char *tmpl) {
  strbuf out;
  sb_init(&out);
  for (const char *p = tmpl; *p;) {
    if (p[0] == '{' && p[1] == '{') {
      sb_appendc(&out, '{');
      p += 2;
      continue;
    }
    if (p[0] == '}' && p[1] == '}') {
      sb_appendc(&out, '}');
      p += 2;
      continue;
    }
    const char *end = p[0] == '{' ? strchr(p + 1, '}') : NULL;
    if (end && end - p - 1 < 32) {
      char key[32];
      size_t n = (size_t)(end - p - 1);
      memcpy(key, p + 1, n);
      key[n] = '\0';
      if (lookup(ctx, key, &out)) {
        p = end + 1;
        continue;
      }
    }
    sb_appendc(&out, *p++);
  }
  /* Collapse surrounding whitespace left by empty placeholders */
  char *s = sb_steal(&out);
  char *t = str_trim(s);
  memmove(s, t, strlen(t) + 1);
  return s;
}

/* ---------- Waybar ---------- */

static void tooltip_hours(strbuf *tt, const render_ctx *ctx) {
  const weather_t *w = ctx->w;
  int n = ctx->cfg->display.hours < w->n_hours ? ctx->cfg->display.hours
                                                : w->n_hours;
  if (n <= 0)
    return;
  sb_append(tt, "\n\n<b>Next hours</b>");
  for (int i = 0; i < n; i++) {
    const weather_hour *h = &w->hours[i];
    char clock[16], temp[16], line[128];
    fmt_clock(clock, sizeof clock, h->time, w->units.clock);
    fmt_number(temp, sizeof temp, h->temperature, ctx->cfg->display.decimals);
    sb_append(tt, "\n<tt>");
    sb_append_padded(tt, clock, 9);
    if (ctx->icons) {
      sb_append(tt, wmo_icon(h->code, h->is_day));
      sb_append(tt, "  ");
    }
    snprintf(line, sizeof line, "%s%s", temp, TEMP_LABELS[w->units.temperature]);
    sb_append_padded(tt, line, 9);
    snprintf(line, sizeof line, "%3d%%  ", h->precip_probability);
    sb_append(tt, line);
    sb_append_markup(tt, wmo_description(h->code));
    sb_append(tt, "</tt>");
  }
}

static void tooltip_days(strbuf *tt, const render_ctx *ctx) {
  const weather_t *w = ctx->w;
  int n = ctx->cfg->display.days < w->n_days ? ctx->cfg->display.days
                                              : w->n_days;
  if (n <= 0)
    return;
  sb_append(tt, "\n\n<b>Next days</b>");
  for (int i = 0; i < n; i++) {
    const weather_day *d = &w->days[i];
    char day[32], lo[16], hi[16], line[128];
    fmt_day(day, sizeof day, w->days, i);
    fmt_number(lo, sizeof lo, d->temp_min, 0);
    fmt_number(hi, sizeof hi, d->temp_max, 0);
    sb_append(tt, "\n<tt>");
    sb_append_padded(tt, day, 9);
    if (ctx->icons) {
      sb_append(tt, wmo_icon(d->code, true));
      sb_append(tt, "  ");
    }
    snprintf(line, sizeof line, "%s° / %s°", lo, hi);
    sb_append_padded(tt, line, 11);
    snprintf(line, sizeof line, "%3d%%  ", d->precip_probability);
    sb_append(tt, line);
    sb_append_markup(tt, wmo_description(d->code));
    sb_append(tt, "</tt>");
  }
}

static void tooltip_line(strbuf *tt, const render_ctx *ctx, const char *icon,
                         const char *tmpl) {
  char *s = render_template(ctx, tmpl);
  sb_append(tt, "\n");
  if (ctx->icons) {
    sb_append(tt, icon);
    sb_append(tt, "  ");
  }
  sb_append_markup(tt, s);
  free(s);
}

static char *build_tooltip(const render_ctx *ctx) {
  strbuf tt;
  sb_init(&tt);
  char *title = render_template(ctx, ctx->cfg->display.tooltip_title);
  sb_append(&tt, "<b>");
  sb_append_markup(&tt, title);
  sb_append(&tt, "</b>");
  free(title);

  char *head = render_template(
      ctx, "{desc} · {temp}{tunit}, feels like {feels}{tunit}");
  sb_append(&tt, "\n");
  if (ctx->icons) {
    sb_append(&tt, "<big>");
    sb_append(&tt, wmo_icon(ctx->w->code, ctx->w->is_day));
    sb_append(&tt, "</big>  ");
  }
  sb_append_markup(&tt, head);
  free(head);
  sb_append(&tt, "\n");

  tooltip_line(&tt, ctx, "\U000F050F",
               "Today {tmin}{tunit} – {tmax}{tunit}");
  tooltip_line(&tt, ctx, "\U000F058E",
               "Humidity {humidity}% · Dew point {dew}{tunit}");
  tooltip_line(&tt, ctx, "\U000F054A",
               "Precipitation {precip} {punit} · Clouds {cloud}%");
  tooltip_line(&tt, ctx, "\U000F059D",
               "Wind {wind} {wunit} {wind_dir} {wind_arrow} · Gusts {gust} "
               "{wunit}");
  tooltip_line(&tt, ctx, "\U000F029A",
               "Pressure {pressure} {presunit} · UV {uv}");
  tooltip_line(&tt, ctx, "\U000F059C", "Sunrise {sunrise} · Sunset {sunset}");

  tooltip_hours(&tt, ctx);
  tooltip_days(&tt, ctx);

  char *foot = render_template(ctx, "Updated {updated} · {source}");
  sb_append(&tt, "\n\n<small>");
  sb_append_markup(&tt, foot);
  if (ctx->w->stale)
    sb_append(&tt, " · <i>offline, showing cached data</i>");
  sb_append(&tt, "</small>");
  free(foot);
  return sb_steal(&tt);
}

char *render_waybar(const render_ctx *ctx) {
  strbuf out;
  sb_init(&out);
  char *text = render_template(ctx, ctx->cfg->display.text);
  sb_append(&out, "{\"text\":\"");
  sb_append_json(&out, text);
  free(text);

  sb_append(&out, "\",\"alt\":\"");
  sb_append_json(&out, wmo_class(ctx->w->code));

  if (ctx->cfg->display.tooltip) {
    char *tt = build_tooltip(ctx);
    sb_append(&out, "\",\"tooltip\":\"");
    sb_append_json(&out, tt);
    free(tt);
  }

  sb_printf(&out, "\",\"class\":[\"%s\",\"%s\",\"%s\"",
            wmo_class(ctx->w->code), ctx->w->is_day ? "day" : "night",
            temp_class(ctx->w));
  if (ctx->w->stale)
    sb_append(&out, ",\"stale\"");
  if (ctx->cfg->geo.enabled)
    sb_append(&out, ",\"geolocated\"");
  sb_printf(&out, "],\"percentage\":%d}",
            (int)CLAMP(isnan(ctx->w->cloud_cover) ? 0 : ctx->w->cloud_cover,
                       0, 100));
  return sb_steal(&out);
}

char *render_waybar_error(const char *text, const char *tooltip) {
  strbuf out;
  sb_init(&out);
  sb_append(&out, "{\"text\":\"");
  sb_append_json(&out, text);
  sb_append(&out, "\",\"tooltip\":\"");
  strbuf esc;
  sb_init(&esc);
  sb_append_markup(&esc, tooltip);
  sb_append_json(&out, esc.data ? esc.data : "");
  sb_free(&esc);
  sb_append(&out, "\",\"alt\":\"error\",\"class\":[\"error\"]}");
  return sb_steal(&out);
}

/* ---------- Terminal report ---------- */

typedef struct {
  const char *bold, *dim, *accent, *blue, *yellow, *reset;
} palette;

static void print_row(FILE *out, const palette *c, const render_ctx *ctx,
                      const char *icon, const char *label, const char *tmpl) {
  char *val = render_template(ctx, tmpl);
  strbuf line;
  sb_init(&line);
  sb_append(&line, "  ");
  if (ctx->icons) {
    sb_append(&line, icon);
    sb_append(&line, "  ");
  }
  sb_append(&line, c->dim);
  sb_append_padded(&line, label, 14);
  sb_append(&line, c->reset);
  sb_append(&line, val);
  fprintf(out, "%s\n", line.data);
  sb_free(&line);
  free(val);
}

void render_print(FILE *out, const render_ctx *ctx, bool color) {
  palette c = {"", "", "", "", "", ""};
  if (color)
    c = (palette){"\033[1m",  "\033[2m",  "\033[1;36m",
                  "\033[34m", "\033[33m", "\033[0m"};
  const weather_t *w = ctx->w;
  int dec = ctx->cfg->display.decimals;
  char place[512], temp[32];
  location_place(ctx->loc, place, sizeof place);
  fmt_number(temp, sizeof temp, w->temperature, dec);

  fprintf(out, "\n  %s%s%s%s%s  %s%s\n", c.accent,
          ctx->icons ? wmo_icon(w->code, w->is_day) : "",
          ctx->icons ? "  " : "", temp, TEMP_LABELS[w->units.temperature],
          wmo_description(w->code), c.reset);
  if (!str_empty(ctx->loc->name) && !str_ieq(ctx->loc->name, ctx->loc->city))
    fprintf(out, "  %s%s%s — %s\n", c.bold, ctx->loc->name, c.reset, place);
  else
    fprintf(out, "  %s%s%s\n", c.bold, place, c.reset);
  char *sub = render_template(
      ctx, "{lat}, {lon} · updated {updated} · source: {source}");
  fprintf(out, "  %s%s%s%s\n\n", c.dim, sub,
          w->stale ? " · OFFLINE (cached)" : "", c.reset);
  free(sub);

  print_row(out, &c, ctx, "\U000F050F", "Feels like", "{feels}{tunit}");
  print_row(out, &c, ctx, "\U000F050F", "Today", "{tmin}{tunit} – {tmax}{tunit}");
  print_row(out, &c, ctx, "\U000F058E", "Humidity",
            "{humidity}% (dew point {dew}{tunit})");
  print_row(out, &c, ctx, "\U000F054A", "Precipitation", "{precip} {punit}");
  print_row(out, &c, ctx, "\U000F0590", "Cloud cover", "{cloud}%");
  print_row(out, &c, ctx, "\U000F059D", "Wind",
            "{wind} {wunit} {wind_dir} {wind_arrow} (gusts {gust} {wunit})");
  print_row(out, &c, ctx, "\U000F029A", "Pressure", "{pressure} {presunit}");
  print_row(out, &c, ctx, "\U000F0599", "UV index", "{uv}");
  print_row(out, &c, ctx, "\U000F0208", "Visibility", "{visibility}");
  print_row(out, &c, ctx, "\U000F059C", "Sun", "↑ {sunrise}  ↓ {sunset}");

  int nh = CLAMP(ctx->cfg->display.hours, 0, w->n_hours);
  if (nh > 0)
    fprintf(out, "\n  %sNext hours%s\n", c.bold, c.reset);
  for (int i = 0; i < nh; i++) {
    const weather_hour *h = &w->hours[i];
    char clock[16], t[16], p[16];
    fmt_clock(clock, sizeof clock, h->time, w->units.clock);
    fmt_number(t, sizeof t, h->temperature, dec);
    fmt_number(p, sizeof p, h->precipitation,
               precip_decimals(&w->units));
    strbuf line;
    sb_init(&line);
    sb_append(&line, "  ");
    sb_append_padded(&line, clock, 10);
    if (ctx->icons) {
      sb_append(&line, wmo_icon(h->code, h->is_day));
      sb_append(&line, "  ");
    }
    char tbuf[32];
    snprintf(tbuf, sizeof tbuf, "%s%s", t, TEMP_LABELS[w->units.temperature]);
    sb_append_padded(&line, tbuf, 9);
    sb_printf(&line, "%s%3d%%%s %s%6s %s%s  %s", c.blue, h->precip_probability,
              c.reset, c.dim, p, PRECIP_LABELS[w->units.precipitation],
              c.reset, wmo_description(h->code));
    fprintf(out, "%s\n", line.data);
    sb_free(&line);
  }

  int nd = CLAMP(ctx->cfg->display.days, 0, w->n_days);
  if (nd > 0)
    fprintf(out, "\n  %sNext days%s\n", c.bold, c.reset);
  for (int i = 0; i < nd; i++) {
    const weather_day *d = &w->days[i];
    char day[32], lo[16], hi[16], p[16];
    fmt_day(day, sizeof day, w->days, i);
    fmt_number(lo, sizeof lo, d->temp_min, 0);
    fmt_number(hi, sizeof hi, d->temp_max, 0);
    fmt_number(p, sizeof p, d->precipitation, precip_decimals(&w->units));
    strbuf line;
    sb_init(&line);
    sb_append(&line, "  ");
    sb_append_padded(&line, day, 10);
    if (ctx->icons) {
      sb_append(&line, wmo_icon(d->code, true));
      sb_append(&line, "  ");
    }
    char range[48];
    snprintf(range, sizeof range, "%s%4s°%s / %s%3s°%s", c.blue, lo, c.reset,
             c.yellow, hi, c.reset);
    sb_append(&line, range);
    sb_printf(&line, "  %s%3d%%%s %s%6s %s%s  %s", c.blue,
              d->precip_probability, c.reset, c.dim, p,
              PRECIP_LABELS[w->units.precipitation], c.reset,
              wmo_description(d->code));
    fprintf(out, "%s\n", line.data);
    sb_free(&line);
  }
  fputc('\n', out);
}
