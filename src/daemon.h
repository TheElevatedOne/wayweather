#ifndef WW_DAEMON_H_
#define WW_DAEMON_H_

#include <stdbool.h>

typedef struct {
  int interval;         /* seconds between refreshes, 0 = from config */
  bool no_icon;
  const char *location; /* pin to a saved location, NULL = follow config */
} daemon_opts;

/* Streams Waybar JSON lines until terminated. Reacts to:
 *   config file changes (inotify)  -> re-render immediately
 *   SIGUSR1                        -> force refresh (bypass caches)
 *   SIGUSR2                        -> switch to the next location
 *   SIGHUP                         -> reload config */
int daemon_run(const daemon_opts *opts);

/* Sends `sig` to every running daemon of this user; returns how many */
int daemon_signal_all(int sig);

#endif // !WW_DAEMON_H_
