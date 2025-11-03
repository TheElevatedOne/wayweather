#include <stdio.h>
#include <stdlib.h>

void logger(const int type, const char *message) {
  char types[4][32] = {"[\033[1;33mLOG\033[0m]", "[\033[1;31mERROR\033[0m]",
                       "[\033[2;38;255;100;0mWARNING\033[0m]",
                       "[\033[1;34mINFO\033[0m]"};
  if (type > (sizeof(types) / sizeof(types[0]))) {
    fprintf(stderr, "%s Invalid Logging Type\n", types[1]);
    exit(1);
  }

  char *ret = malloc(128);
  sprintf(ret, "%s %s", types[type], message);
  fprintf(stderr, "%s\n", ret);
  free(ret);
}
