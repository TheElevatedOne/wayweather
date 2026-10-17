#ifndef WW_HTTP_H_
#define WW_HTTP_H_

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *body;
  size_t len;
  long status;
} http_response;

void http_init(void);
void http_cleanup(void);

/* Performs a GET request. On failure returns false and writes a
 * human readable reason into `err`. */
bool http_get(const char *url, long timeout_s, http_response *out, char *err,
              size_t errlen);
void http_response_free(http_response *res);

/* URL-encodes `s` (caller frees) */
char *http_escape(const char *s);

#endif // !WW_HTTP_H_
