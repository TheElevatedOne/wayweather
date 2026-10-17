#define _GNU_SOURCE
#include "geolocate.h"
#include "geocode.h"
#include "http.h"
#include "log.h"
#include "util.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_GEOCLUE
#include <geoclue.h>
#endif

typedef struct {
  double lat;
  double lon;
  bool has_place; /* provider already returned city/country */
  location_t place;
} fix_t;

/* ---------- gpsd (JSON protocol over TCP, no libgps needed) ---------- */

static int connect_timeout(const char *host, int port, int timeout_ms) {
  char portstr[16];
  snprintf(portstr, sizeof portstr, "%d", port);
  struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, portstr, &hints, &res) != 0)
    return -1;

  int fd = -1;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                ai->ai_protocol);
    if (fd < 0)
      continue;
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    if (errno == EINPROGRESS) {
      struct pollfd p = {.fd = fd, .events = POLLOUT};
      int soerr = 0;
      socklen_t len = sizeof soerr;
      if (poll(&p, 1, timeout_ms) == 1 &&
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) == 0 &&
          soerr == 0)
        break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

static bool tpv_fix(const cJSON *tpv, fix_t *fix) {
  const cJSON *mode = cJSON_GetObjectItemCaseSensitive(tpv, "mode");
  const cJSON *lat = cJSON_GetObjectItemCaseSensitive(tpv, "lat");
  const cJSON *lon = cJSON_GetObjectItemCaseSensitive(tpv, "lon");
  if (!cJSON_IsNumber(mode) || mode->valueint < 2 || !cJSON_IsNumber(lat) ||
      !cJSON_IsNumber(lon))
    return false;
  fix->lat = lat->valuedouble;
  fix->lon = lon->valuedouble;
  return true;
}

static bool gpsd_line(const char *line, fix_t *fix) {
  cJSON *msg = cJSON_Parse(line);
  if (!msg)
    return false;
  bool ok = false;
  const cJSON *cls = cJSON_GetObjectItemCaseSensitive(msg, "class");
  if (cJSON_IsString(cls)) {
    if (strcmp(cls->valuestring, "TPV") == 0) {
      ok = tpv_fix(msg, fix);
    } else if (strcmp(cls->valuestring, "POLL") == 0) {
      const cJSON *tpv;
      cJSON_ArrayForEach(tpv, cJSON_GetObjectItemCaseSensitive(msg, "tpv")) {
        if ((ok = tpv_fix(tpv, fix)))
          break;
      }
    }
  }
  cJSON_Delete(msg);
  return ok;
}

static bool locate_gpsd(const geo_config *geo, fix_t *fix, char *err,
                        size_t errlen) {
  int timeout_ms = geo->timeout * 1000;
  int fd = connect_timeout(geo->gpsd_host, geo->gpsd_port, 1000);
  if (fd < 0) {
    snprintf(err, errlen, "gpsd is not reachable at %s:%d", geo->gpsd_host,
             geo->gpsd_port);
    return false;
  }
  static const char cmd[] = "?WATCH={\"enable\":true,\"json\":true};?POLL;\n";
  if (write(fd, cmd, sizeof cmd - 1) < 0) {
    snprintf(err, errlen, "gpsd: %s", strerror(errno));
    close(fd);
    return false;
  }

  strbuf buf;
  sb_init(&buf);
  bool ok = false;
  long long deadline = now_ms() + timeout_ms;
  while (!ok) {
    long long left = deadline - now_ms();
    if (left <= 0)
      break;
    struct pollfd p = {.fd = fd, .events = POLLIN};
    if (poll(&p, 1, (int)left) <= 0)
      break;
    char chunk[4096];
    ssize_t n = read(fd, chunk, sizeof chunk);
    if (n <= 0)
      break;
    sb_appendn(&buf, chunk, (size_t)n);
    char *nl;
    while (!ok && (nl = memchr(buf.data, '\n', buf.len))) {
      *nl = '\0';
      ok = gpsd_line(buf.data, fix);
      size_t consumed = (size_t)(nl - buf.data) + 1;
      memmove(buf.data, nl + 1, buf.len - consumed + 1);
      buf.len -= consumed;
    }
  }
  sb_free(&buf);
  close(fd);
  if (!ok)
    snprintf(err, errlen, "gpsd has no position fix (waited %ds)",
             geo->timeout);
  return ok;
}

/* ---------- GeoClue2 ---------- */

#ifdef HAVE_GEOCLUE
typedef struct {
  GMainLoop *loop;
  GClueSimple *simple;
  GError *error;
  bool done;
} gc_ctx;

static void gc_ready(GObject *src, GAsyncResult *res, gpointer data) {
  (void)src;
  gc_ctx *c = data;
  c->simple = gclue_simple_new_finish(res, &c->error);
  c->done = true;
  g_main_loop_quit(c->loop);
}

static gboolean gc_timeout(gpointer data) {
  g_main_loop_quit(((gc_ctx *)data)->loop);
  return G_SOURCE_REMOVE;
}

static bool locate_geoclue(const geo_config *geo, fix_t *fix, char *err,
                           size_t errlen) {
  gc_ctx c = {.loop = g_main_loop_new(NULL, FALSE)};
  GCancellable *cancel = g_cancellable_new();
  gclue_simple_new("wayweather", GCLUE_ACCURACY_LEVEL_EXACT, cancel, gc_ready,
                   &c);
  guint tid = g_timeout_add_seconds((guint)geo->timeout, gc_timeout, &c);
  g_main_loop_run(c.loop);
  if (!c.done) {
    g_cancellable_cancel(cancel);
    while (!c.done)
      g_main_context_iteration(NULL, TRUE);
  } else {
    g_source_remove(tid);
  }

  bool ok = false;
  if (c.simple) {
    GClueLocation *l = gclue_simple_get_location(c.simple);
    if (l) {
      fix->lat = gclue_location_get_latitude(l);
      fix->lon = gclue_location_get_longitude(l);
      ok = true;
    }
    g_object_unref(c.simple);
  }
  if (!ok)
    snprintf(err, errlen, "GeoClue: %s",
             c.error ? c.error->message : "no location within timeout");
  if (c.error)
    g_error_free(c.error);
  g_object_unref(cancel);
  g_main_loop_unref(c.loop);
  return ok;
}
#endif

bool geolocate_has_geoclue(void) {
#ifdef HAVE_GEOCLUE
  return true;
#else
  return false;
#endif
}

/* ---------- IP geolocation ---------- */

static const char *jstr(const cJSON *o, const char *key) {
  const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(it) ? it->valuestring : "";
}

static bool ip_service(const char *url, const char *lat_key,
                       const char *lon_key, const char *region_key,
                       const geo_config *geo, fix_t *fix, char *err,
                       size_t errlen) {
  http_response res;
  bool ok = http_get(url, geo->timeout, &res, err, errlen);
  cJSON *root = ok ? cJSON_Parse(res.body) : NULL;
  http_response_free(&res);
  if (!root) {
    if (ok)
      snprintf(err, errlen, "invalid response from %s", url);
    return false;
  }
  const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, lat_key);
  const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, lon_key);
  ok = cJSON_IsNumber(lat) && cJSON_IsNumber(lon);
  if (ok) {
    fix->lat = lat->valuedouble;
    fix->lon = lon->valuedouble;
    location_init(&fix->place);
    str_copy(fix->place.city, jstr(root, "city"), sizeof fix->place.city);
    str_copy(fix->place.region, jstr(root, region_key),
             sizeof fix->place.region);
    str_copy(fix->place.country, jstr(root, "country"),
             sizeof fix->place.country);
    fix->has_place = !str_empty(fix->place.city);
  } else {
    const char *msg = jstr(root, "message");
    snprintf(err, errlen, "%s", str_empty(msg) ? "no coordinates" : msg);
  }
  cJSON_Delete(root);
  return ok;
}

static bool locate_ip(const geo_config *geo, fix_t *fix, char *err,
                      size_t errlen) {
  char e1[128] = "", e2[128] = "";
  /* ip-api.com is only free over plain HTTP; ipwho.is is the HTTPS backup */
  if (ip_service("http://ip-api.com/json?fields=status,message,lat,lon,city,"
                 "regionName,country",
                 "lat", "lon", "regionName", geo, fix, e1, sizeof e1))
    return true;
  log_debug("ip-api.com failed: %s", e1);
  if (ip_service("https://ipwho.is/", "latitude", "longitude", "region", geo,
                 fix, e2, sizeof e2))
    return true;
  snprintf(err, errlen, "IP geolocation failed (%s; %s)", e1, e2);
  return false;
}

/* ---------- Cache ---------- */

static char *cache_path(void) {
  return xasprintf("%s/geolocation.json", xdg_cache_dir());
}

static bool cache_load(const geo_config *geo, location_t *out, char *source,
                       size_t source_len) {
  char *path = cache_path();
  char *text = read_file(path, NULL);
  free(path);
  if (!text)
    return false;
  cJSON *root = cJSON_Parse(text);
  free(text);
  const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "fetched_at");
  const cJSON *prov = cJSON_GetObjectItemCaseSensitive(root, "provider");
  const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "latitude");
  const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "longitude");
  bool ok = cJSON_IsNumber(ts) && cJSON_IsNumber(lat) && cJSON_IsNumber(lon) &&
            cJSON_IsString(prov) &&
            strcmp(prov->valuestring, GEO_PROVIDER_IDS[geo->provider]) == 0 &&
            time(NULL) - (time_t)ts->valuedouble < geo->cache_ttl;
  if (ok) {
    out->latitude = lat->valuedouble;
    out->longitude = lon->valuedouble;
    str_copy(out->city, jstr(root, "city"), sizeof out->city);
    str_copy(out->region, jstr(root, "region"), sizeof out->region);
    str_copy(out->country, jstr(root, "country"), sizeof out->country);
    snprintf(source, source_len, "%s (cached)", jstr(root, "source"));
  }
  cJSON_Delete(root);
  return ok;
}

static void cache_store(const geo_config *geo, const location_t *loc,
                        const char *source) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "fetched_at", (double)time(NULL));
  cJSON_AddStringToObject(root, "provider", GEO_PROVIDER_IDS[geo->provider]);
  cJSON_AddStringToObject(root, "source", source);
  cJSON_AddNumberToObject(root, "latitude", loc->latitude);
  cJSON_AddNumberToObject(root, "longitude", loc->longitude);
  cJSON_AddStringToObject(root, "city", loc->city);
  cJSON_AddStringToObject(root, "region", loc->region);
  cJSON_AddStringToObject(root, "country", loc->country);
  char *text = cJSON_Print(root);
  char *path = cache_path();
  write_file_atomic(path, text, strlen(text));
  free(path);
  cJSON_free(text);
  cJSON_Delete(root);
}

/* ---------- Entry point ---------- */

bool geolocate(const geo_config *geo, bool force, location_t *out,
               char *source, size_t source_len, char *err, size_t errlen) {
  if (!force && geo->cache_ttl > 0 &&
      cache_load(geo, out, source, source_len)) {
    log_debug("Using cached geolocation from %s", source);
    return true;
  }

  typedef bool (*provider_fn)(const geo_config *, fix_t *, char *, size_t);
  struct {
    geo_provider id;
    const char *name;
    provider_fn fn;
  } chain[] = {
      {GEO_GPSD, "gpsd", locate_gpsd},
#ifdef HAVE_GEOCLUE
      {GEO_GEOCLUE, "geoclue", locate_geoclue},
#endif
      {GEO_IP, "ip", locate_ip},
  };

  if (geo->provider == GEO_GEOCLUE && !geolocate_has_geoclue()) {
    snprintf(err, errlen, "this build has no GeoClue support "
                          "(rebuild with GEOCLUE=1)");
    return false;
  }

  strbuf errors;
  sb_init(&errors);
  fix_t fix = {0};
  const char *used = NULL;
  for (size_t i = 0; i < ARRAY_LEN(chain) && !used; i++) {
    if (geo->provider != GEO_AUTO && geo->provider != chain[i].id)
      continue;
    char e[256] = "";
    long long start = now_ms();
    if (chain[i].fn(geo, &fix, e, sizeof e)) {
      used = chain[i].name;
      log_debug("Located via %s in %lld ms: %.4f, %.4f", used,
                now_ms() - start, fix.lat, fix.lon);
    } else {
      log_debug("%s: %s", chain[i].name, e);
      if (errors.len)
        sb_append(&errors, "; ");
      sb_append(&errors, e);
    }
  }
  if (!used) {
    snprintf(err, errlen, "%s", errors.len ? errors.data : "no provider");
    sb_free(&errors);
    return false;
  }
  sb_free(&errors);

  out->latitude = fix.lat;
  out->longitude = fix.lon;
  out->city[0] = out->region[0] = out->country[0] = '\0';
  if (fix.has_place) {
    str_copy(out->city, fix.place.city, sizeof out->city);
    str_copy(out->region, fix.place.region, sizeof out->region);
    str_copy(out->country, fix.place.country, sizeof out->country);
  } else if (geo->reverse_geocode) {
    geocode_reverse(fix.lat, fix.lon, out);
  }
  snprintf(source, source_len, "%s", used);
  cache_store(geo, out, used);
  return true;
}
