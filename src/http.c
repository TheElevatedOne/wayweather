#include "http.h"
#include "log.h"
#include "util.h"
#include "version.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Responses larger than this are certainly not what we asked for */
#define MAX_BODY (8u * 1024 * 1024)

static size_t on_write(void *data, size_t size, size_t nmemb, void *userp) {
  strbuf *sb = userp;
  size_t n = size * nmemb;
  if (sb->len + n > MAX_BODY)
    return 0;
  sb_appendn(sb, data, n);
  return n;
}

void http_init(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

void http_cleanup(void) { curl_global_cleanup(); }

bool http_get(const char *url, long timeout_s, http_response *out, char *err,
              size_t errlen) {
  memset(out, 0, sizeof *out);
  CURL *curl = curl_easy_init();
  if (!curl) {
    snprintf(err, errlen, "could not initialise libcurl");
    return false;
  }

  strbuf body;
  sb_init(&body);
  char curl_err[CURL_ERROR_SIZE] = "";
  long connect_timeout = timeout_s < 5 ? timeout_s : 5;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "wayweather/" WW_VERSION " (+" WW_URL ")");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err);

  log_debug("GET %s", url);
  long long start = now_ms();
  CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
  curl_easy_cleanup(curl);
  log_debug("GET finished in %lld ms (HTTP %ld)", now_ms() - start,
            out->status);

  if (rc != CURLE_OK) {
    snprintf(err, errlen, "%s",
             curl_err[0] ? curl_err : curl_easy_strerror(rc));
    sb_free(&body);
    return false;
  }
  out->len = body.len;
  out->body = sb_steal(&body);
  if (out->status >= 400) {
    snprintf(err, errlen, "server returned HTTP %ld", out->status);
    return false;
  }
  return true;
}

void http_response_free(http_response *res) {
  free(res->body);
  res->body = NULL;
  res->len = 0;
}

char *http_escape(const char *s) {
  char *esc = curl_easy_escape(NULL, s, 0);
  char *out = xstrdup(esc);
  curl_free(esc);
  return out;
}
