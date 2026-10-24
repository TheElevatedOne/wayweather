#define _GNU_SOURCE
#include "daemon.h"
#include "app.h"
#include "config.h"
#include "log.h"
#include "render.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <unistd.h>

#define RETRY_MIN 30
#define DEBOUNCE_MS 150

/* ---------- Registry of running daemons ---------- */

static const char *registry_dir(void) {
  static char path[PATH_MAX];
  if (!path[0])
    snprintf(path, sizeof path, "%s/wayweather", xdg_runtime_dir());
  return path;
}

static char pid_file[PATH_MAX];

static void registry_add(void) {
  if (!mkdir_p(registry_dir(), 0700))
    return;
  snprintf(pid_file, sizeof pid_file, "%s/daemon.%d", registry_dir(),
           (int)getpid());
  FILE *f = fopen(pid_file, "w");
  if (f) {
    fprintf(f, "%d\n", (int)getpid());
    fclose(f);
  }
}

static void registry_remove(void) {
  if (pid_file[0])
    unlink(pid_file);
}

int daemon_signal_all(int sig) {
  DIR *dir = opendir(registry_dir());
  if (!dir)
    return 0;
  int count = 0;
  struct dirent *de;
  while ((de = readdir(dir))) {
    long pid;
    if (strncmp(de->d_name, "daemon.", 7) != 0 ||
        !parse_long(de->d_name + 7, &pid) || pid <= 0)
      continue;
    if (kill((pid_t)pid, sig) == 0) {
      count++;
    } else if (errno == ESRCH) {
      char *stale = xasprintf("%s/%s", registry_dir(), de->d_name);
      unlink(stale);
      free(stale);
    }
  }
  closedir(dir);
  return count;
}

/* ---------- Main loop ---------- */

static bool emit(const char *line) {
  if (printf("%s\n", line) < 0 || fflush(stdout) != 0)
    return false; /* Waybar went away */
  return true;
}

static int inotify_setup(char *basename, size_t size) {
  /* Watch the real file: a symlinked config (dotfile managers) is written
   * through to its target, so events happen in the target's directory */
  char dir[PATH_MAX];
  if (!realpath(config_path(), dir))
    str_copy(dir, config_path(), sizeof dir);
  char *slash = strrchr(dir, '/');
  if (!slash)
    return -1;
  *slash = '\0';
  str_copy(basename, slash + 1, size);
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0)
    return -1;
  if (inotify_add_watch(fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) <
      0) {
    log_warn("Cannot watch %s for changes: %s", dir, strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

/* Drains inotify; true if the config file was touched */
static bool inotify_config_changed(int fd, const char *basename) {
  char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
  bool changed = false;
  ssize_t n;
  while ((n = read(fd, buf, sizeof buf)) > 0) {
    for (char *p = buf; p < buf + n;) {
      struct inotify_event *ev = (struct inotify_event *)p;
      if (ev->len && strcmp(ev->name, basename) == 0)
        changed = true;
      p += sizeof *ev + ev->len;
    }
  }
  return changed;
}

static void cycle_next(void) {
  int lock = config_lock();
  config_t cfg;
  if (config_load(&cfg)) {
    char desc[128];
    if (app_cycle(&cfg, 1, desc, sizeof desc) && config_save(&cfg))
      log_info("Switched to %s", desc);
  }
  config_free(&cfg);
  config_unlock(lock);
}

int daemon_run(const daemon_opts *opts) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  sigaddset(&mask, SIGHUP);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, NULL);
  signal(SIGPIPE, SIG_IGN);
  int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

  /* Load first: it creates the config directory that inotify watches */
  config_t cfg;
  bool have_cfg = config_load(&cfg);
  char basename[NAME_MAX + 1];
  int ifd = inotify_setup(basename, sizeof basename);
  registry_add();
  atexit(registry_remove);
  bool force = false;
  int failures = 0;
  char *last = NULL;
  int status = 0;

  for (;;) {
    int interval = opts->interval > 0 ? opts->interval : cfg.refresh_interval;
    int wait_s = interval;

    if (!have_cfg) {
      char *line = render_waybar_error("?", "wayweather: the config file is "
                                            "invalid, run \"wayweather config "
                                            "check\"");
      if (!emit(line)) {
        free(line);
        break;
      }
      free(line);
    } else {
      if (opts->no_icon)
        cfg.display.icons = false;
      location_t loc;
      weather_t w;
      char source[96], err[512];
      /* A refresh tick must refetch; several bars share the cache though */
      int ttl = interval - 5 < cfg.cache_ttl ? interval - 5 : cfg.cache_ttl;
      bool ok = app_fetch(&cfg, opts->location, force, ttl, &loc, &w, source,
                          sizeof source, err, sizeof err);
      char *line;
      if (ok) {
        render_ctx ctx = {&cfg, &loc, &w, source, cfg.display.icons};
        line = render_waybar(&ctx);
        failures = w.stale ? failures + 1 : 0;
      } else {
        log_error("%s", err);
        line = render_waybar_error(cfg.display.icons ? "\U000F0F2F" : "?",
                                   err);
        failures++;
      }
      if (failures > 0) {
        /* Retry sooner while offline: 30s, 60s, 120s, ... up to interval */
        int retry = RETRY_MIN << (failures > 5 ? 5 : failures - 1);
        wait_s = retry < interval ? retry : interval;
      }
      if (!last || strcmp(last, line) != 0) {
        if (!emit(line)) {
          free(line);
          break;
        }
        free(last);
        last = line;
      } else {
        free(line);
      }
    }
    force = false;

    /* Wait for the timer, a config change or a signal */
    bool reload = false, wake = false;
    long long deadline = now_ms() + (long long)wait_s * 1000;
    while (!wake) {
      long long left = deadline - now_ms();
      if (left <= 0)
        break;
      struct pollfd fds[2] = {{.fd = sfd, .events = POLLIN},
                              {.fd = ifd, .events = POLLIN}};
      int rc = poll(fds, ifd >= 0 ? 2 : 1, (int)(left > INT_MAX ? INT_MAX : left));
      if (rc < 0 && errno != EINTR)
        break;
      if (rc <= 0)
        continue;

      if (fds[0].revents & POLLIN) {
        struct signalfd_siginfo si;
        while (read(sfd, &si, sizeof si) == sizeof si) {
          switch (si.ssi_signo) {
          case SIGUSR1:
            log_info("Refresh requested");
            force = wake = true;
            break;
          case SIGUSR2:
            cycle_next(); /* inotify picks up the saved config */
            if (ifd < 0)
              reload = wake = true;
            break;
          case SIGHUP:
            reload = wake = true;
            break;
          default:
            goto out;
          }
        }
      }
      if (ifd >= 0 && (fds[1].revents & POLLIN)) {
        /* Editors write in several steps: wait until things settle */
        bool changed = inotify_config_changed(ifd, basename);
        struct pollfd p = {.fd = ifd, .events = POLLIN};
        while (poll(&p, 1, DEBOUNCE_MS) > 0)
          changed |= inotify_config_changed(ifd, basename);
        if (changed) {
          log_info("Config changed, reloading");
          reload = wake = true;
        }
      }
    }

    if (reload) {
      config_t fresh;
      if (config_load(&fresh)) {
        config_free(&cfg);
        cfg = fresh;
        have_cfg = true;
      } else {
        config_free(&fresh);
        log_error("Keeping the previous config");
      }
    }
  }

out:
  free(last);
  config_free(&cfg);
  if (ifd >= 0)
    close(ifd);
  if (sfd >= 0)
    close(sfd);
  return status;
}
