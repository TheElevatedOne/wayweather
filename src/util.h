#ifndef WW_UTIL_H_
#define WW_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

/* Allocation helpers, abort on out-of-memory */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *ptr, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Growable string buffer */
typedef struct {
  char *data;
  size_t len;
  size_t cap;
} strbuf;

void sb_init(strbuf *sb);
void sb_free(strbuf *sb);
void sb_append(strbuf *sb, const char *s);
void sb_appendn(strbuf *sb, const char *s, size_t n);
void sb_appendc(strbuf *sb, char c);
void sb_printf(strbuf *sb, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
/* Appends `s` escaped for use inside a JSON string literal */
void sb_append_json(strbuf *sb, const char *s);
/* Appends `s` escaped for Pango markup (Waybar tooltips) */
void sb_append_markup(strbuf *sb, const char *s);
/* Returns the buffer (caller frees) and resets `sb` */
char *sb_steal(strbuf *sb);

/* Strings */
size_t str_copy(char *dst, const char *src, size_t size);
bool str_ieq(const char *a, const char *b);
bool str_icontains(const char *haystack, const char *needle);
bool str_empty(const char *s);
char *str_trim(char *s);
bool parse_double(const char *s, double *out);
bool parse_long(const char *s, long *out);

/* UTF-8 aware display width helpers */
int utf8_width(const char *s);
/* Number of bytes of `s` that fit into `cols` terminal columns */
size_t utf8_fit(const char *s, int cols);

/* Filesystem */
const char *xdg_config_dir(void); /* $XDG_CONFIG_HOME/wayweather */
const char *xdg_cache_dir(void);  /* $XDG_CACHE_HOME/wayweather */
const char *xdg_runtime_dir(void);
bool mkdir_p(const char *path, mode_t mode);
bool file_exists(const char *path);
char *read_file(const char *path, size_t *len);
/* Writes via temp file + rename so readers never see partial files */
bool write_file_atomic(const char *path, const char *data, size_t len);

/* Monotonic clock in milliseconds */
long long now_ms(void);

#endif // !WW_UTIL_H_
