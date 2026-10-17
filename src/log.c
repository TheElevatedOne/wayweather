#include "log.h"
#include "util.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int log_level = LOG_WARN;

static bool use_color = false;
static bool capturing = false;
static char last[512];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void log_init(void) {
  use_color = isatty(STDERR_FILENO) && str_empty(getenv("NO_COLOR"));
}

void log_set_color(bool color) { use_color = color; }

void log_capture(bool on) { capturing = on; }

const char *log_last(void) { return last; }

void log_clear_last(void) {
  pthread_mutex_lock(&lock);
  last[0] = '\0';
  pthread_mutex_unlock(&lock);
}

void log_msg(int level, const char *fmt, ...) {
  if (level > log_level && !(capturing && level <= LOG_WARN))
    return;

  static const char *const names[] = {"ERROR", "WARN", "INFO", "DEBUG"};
  static const char *const colors[] = {"\033[1;31m", "\033[1;33m",
                                       "\033[1;34m", "\033[2m"};
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  pthread_mutex_lock(&lock);
  if (capturing) {
    if (level <= LOG_WARN)
      str_copy(last, msg, sizeof last);
  } else if (use_color) {
    fprintf(stderr, "[%s%s\033[0m] %s\n", colors[level], names[level], msg);
  } else {
    fprintf(stderr, "[%s] %s\n", names[level], msg);
  }
  pthread_mutex_unlock(&lock);
}
