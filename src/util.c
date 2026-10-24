#define _GNU_SOURCE
#include "util.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

/* ---------- Allocation ---------- */

static void oom(void) {
  fputs("wayweather: out of memory\n", stderr);
  abort();
}

void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p)
    oom();
  return p;
}

void *xcalloc(size_t n, size_t size) {
  void *p = calloc(n ? n : 1, size ? size : 1);
  if (!p)
    oom();
  return p;
}

void *xrealloc(void *ptr, size_t n) {
  void *p = realloc(ptr, n ? n : 1);
  if (!p)
    oom();
  return p;
}

char *xstrdup(const char *s) {
  char *p = strdup(s ? s : "");
  if (!p)
    oom();
  return p;
}

char *xasprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char *out = NULL;
  if (vasprintf(&out, fmt, ap) < 0)
    oom();
  va_end(ap);
  return out;
}

/* ---------- String buffer ---------- */

void sb_init(strbuf *sb) {
  sb->data = NULL;
  sb->len = 0;
  sb->cap = 0;
}

void sb_free(strbuf *sb) {
  free(sb->data);
  sb_init(sb);
}

static void sb_reserve(strbuf *sb, size_t extra) {
  if (sb->len + extra + 1 <= sb->cap)
    return;
  size_t cap = sb->cap ? sb->cap : 64;
  while (cap < sb->len + extra + 1)
    cap *= 2;
  sb->data = xrealloc(sb->data, cap);
  sb->cap = cap;
}

void sb_appendn(strbuf *sb, const char *s, size_t n) {
  sb_reserve(sb, n);
  memcpy(sb->data + sb->len, s, n);
  sb->len += n;
  sb->data[sb->len] = '\0';
}

void sb_append(strbuf *sb, const char *s) {
  if (s)
    sb_appendn(sb, s, strlen(s));
}

void sb_appendc(strbuf *sb, char c) { sb_appendn(sb, &c, 1); }

void sb_printf(strbuf *sb, const char *fmt, ...) {
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n > 0) {
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    sb->len += (size_t)n;
  }
  va_end(ap2);
}

void sb_append_json(strbuf *sb, const char *s) {
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    switch (c) {
    case '"':
      sb_append(sb, "\\\"");
      break;
    case '\\':
      sb_append(sb, "\\\\");
      break;
    case '\n':
      sb_append(sb, "\\n");
      break;
    case '\r':
      sb_append(sb, "\\r");
      break;
    case '\t':
      sb_append(sb, "\\t");
      break;
    default:
      if (c < 0x20)
        sb_printf(sb, "\\u%04x", c);
      else
        sb_appendc(sb, (char)c);
    }
  }
}

void sb_append_markup(strbuf *sb, const char *s) {
  for (; s && *s; s++) {
    switch (*s) {
    case '&':
      sb_append(sb, "&amp;");
      break;
    case '<':
      sb_append(sb, "&lt;");
      break;
    case '>':
      sb_append(sb, "&gt;");
      break;
    default:
      sb_appendc(sb, *s);
    }
  }
}

char *sb_steal(strbuf *sb) {
  char *out = sb->data ? sb->data : xstrdup("");
  sb_init(sb);
  return out;
}

/* ---------- Strings ---------- */

size_t str_copy(char *dst, const char *src, size_t size) {
  if (!src)
    src = "";
  size_t len = strlen(src);
  if (size == 0)
    return len;
  size_t n = len < size - 1 ? len : size - 1;
  /* Never split a UTF-8 sequence when truncating */
  while (n > 0 && n < len && ((unsigned char)src[n] & 0xC0) == 0x80)
    n--;
  memcpy(dst, src, n);
  dst[n] = '\0';
  return len;
}

bool str_ieq(const char *a, const char *b) {
  return a && b && strcasecmp(a, b) == 0;
}

bool str_icontains(const char *haystack, const char *needle) {
  if (!needle || !*needle)
    return true;
  return haystack && strcasestr(haystack, needle) != NULL;
}

bool str_empty(const char *s) { return !s || !*s; }

char *str_trim(char *s) {
  while (isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1]))
    *--end = '\0';
  return s;
}

bool parse_double(const char *s, double *out) {
  if (str_empty(s))
    return false;
  char *end;
  errno = 0;
  double v = strtod(s, &end);
  while (isspace((unsigned char)*end))
    end++;
  if (errno || *end || end == s)
    return false;
  *out = v;
  return true;
}

bool parse_long(const char *s, long *out) {
  if (str_empty(s))
    return false;
  char *end;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (errno || *end || end == s)
    return false;
  *out = v;
  return true;
}

/* ---------- UTF-8 ---------- */

static int char_width(const char *s, size_t *bytes) {
  mbstate_t st = {0};
  wchar_t wc;
  size_t n = mbrtowc(&wc, s, MB_CUR_MAX, &st);
  if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
    *bytes = 1;
    return 1;
  }
  *bytes = n;
  int w = wcwidth(wc);
  return w < 0 ? 1 : w;
}

int utf8_width(const char *s) {
  int w = 0;
  while (s && *s) {
    size_t n;
    w += char_width(s, &n);
    s += n;
  }
  return w;
}

size_t utf8_fit(const char *s, int cols) {
  const char *p = s;
  int w = 0;
  while (*p) {
    size_t n;
    int cw = char_width(p, &n);
    if (w + cw > cols)
      break;
    w += cw;
    p += n;
  }
  return (size_t)(p - s);
}

/* ---------- Filesystem ---------- */

static void xdg_dir(char *buf, size_t size, const char *env,
                   const char *fallback) {
  const char *base = getenv(env);
  if (!str_empty(base) && base[0] == '/') {
    snprintf(buf, size, "%s/wayweather", base);
    return;
  }
  const char *home = getenv("HOME");
  if (str_empty(home))
    home = "/tmp";
  snprintf(buf, size, "%s/%s/wayweather", home, fallback);
}

const char *xdg_config_dir(void) {
  static char path[PATH_MAX];
  if (!path[0])
    xdg_dir(path, sizeof path, "XDG_CONFIG_HOME", ".config");
  return path;
}

const char *xdg_cache_dir(void) {
  static char path[PATH_MAX];
  if (!path[0])
    xdg_dir(path, sizeof path, "XDG_CACHE_HOME", ".cache");
  return path;
}

const char *xdg_runtime_dir(void) {
  static char path[PATH_MAX];
  if (!path[0]) {
    const char *base = getenv("XDG_RUNTIME_DIR");
    if (!str_empty(base))
      snprintf(path, sizeof path, "%s", base);
    else
      snprintf(path, sizeof path, "/tmp/wayweather-%u", (unsigned)getuid());
  }
  return path;
}

bool mkdir_p(const char *path, mode_t mode) {
  char tmp[PATH_MAX];
  if (str_copy(tmp, path, sizeof tmp) >= sizeof tmp)
    return false;
  for (char *p = tmp + 1; *p; p++) {
    if (*p != '/')
      continue;
    *p = '\0';
    if (mkdir(tmp, mode) != 0 && errno != EEXIST)
      return false;
    *p = '/';
  }
  if (mkdir(tmp, mode) != 0 && errno != EEXIST)
    return false;
  return true;
}

bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  strbuf sb;
  sb_init(&sb);
  char chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
    sb_appendn(&sb, chunk, n);
  bool failed = ferror(f);
  fclose(f);
  if (failed) {
    sb_free(&sb);
    return NULL;
  }
  if (len)
    *len = sb.len;
  return sb_steal(&sb);
}

bool write_file_atomic(const char *path, const char *data, size_t len) {
  /* Replacing a symlink would break dotfile managers: write its target */
  char real[PATH_MAX];
  struct stat st;
  if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode) && realpath(path, real))
    path = real;

  char dir[PATH_MAX];
  str_copy(dir, path, sizeof dir);
  char *slash = strrchr(dir, '/');
  if (slash && slash != dir) {
    *slash = '\0';
    if (!mkdir_p(dir, 0700)) {
      log_error("Cannot create directory %s: %s", dir, strerror(errno));
      return false;
    }
  }

  /* Unique per process and thread (the TUI writes caches from a worker) */
  static atomic_uint counter;
  char *tmp = xasprintf("%s.tmp.%d.%u", path, (int)getpid(),
                        atomic_fetch_add(&counter, 1));
  FILE *f = fopen(tmp, "wb");
  if (!f) {
    log_error("Cannot write %s: %s", tmp, strerror(errno));
    free(tmp);
    return false;
  }
  bool ok = fwrite(data, 1, len, f) == len;
  ok = (fflush(f) == 0) && ok;
  ok = (fsync(fileno(f)) == 0) && ok;
  ok = (fclose(f) == 0) && ok;
  if (ok && rename(tmp, path) != 0)
    ok = false;
  if (!ok) {
    log_error("Failed to write %s: %s", path, strerror(errno));
    unlink(tmp);
  }
  free(tmp);
  return ok;
}

long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
