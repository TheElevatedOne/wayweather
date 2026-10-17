#ifndef WW_LOG_H_
#define WW_LOG_H_

#include <stdbool.h>

enum log_level { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG };

/* Messages above this level are dropped (default: LOG_WARN) */
extern int log_level;

void log_init(void);
/* Enable/disable ANSI colors on stderr (auto-detected by log_init) */
void log_set_color(bool color);
/* When capturing, messages are stored instead of printed (used by the TUI) */
void log_capture(bool on);
/* Last captured error/warning, or "" */
const char *log_last(void);
void log_clear_last(void);

void log_msg(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define log_error(...) log_msg(LOG_ERROR, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_WARN, __VA_ARGS__)
#define log_info(...) log_msg(LOG_INFO, __VA_ARGS__)
#define log_debug(...) log_msg(LOG_DEBUG, __VA_ARGS__)

#endif // !WW_LOG_H_
