/* plex/http.c: libcurl wrapper + the standard X-Plex-* header set.
 *
 * Every request carries the client identity headers. Two PMS 1.43.3 quirks
 * are baked in (verified against the owner's server):
 *  - X-Plex-Platform must be a profile the transcoder knows; "Chrome" works,
 *    "Linux" makes the universal transcode endpoints return 400.
 *  - JSON only comes back when Accept: application/json is sent.
 * The token goes in the X-Plex-Token header, not the URL (keeps URLs out of
 * logs; transcode URLs built for a player process are the one exception and
 * log.h redacts their token= query form).
 */
#include "plex/internal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define PP_PRODUCT   "PocketPlex"
#define PP_VERSION   "0.1"
#define PP_PLATFORM  "Chrome"

static char g_client_id[64] = "";

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
  pp_http_response *r = ud;
  size_t len = size * nmemb;
  char *grown = realloc(r->body, r->size + len + 1);
  if (!grown) return 0; /* aborts the transfer with CURLE_WRITE_ERROR */
  r->body = grown;
  memcpy(r->body + r->size, ptr, len);
  r->size += len;
  r->body[r->size] = '\0';
  return len;
}

static struct curl_slist *appendf(struct curl_slist *h, const char *fmt, const char *a)
    __attribute__((format(printf, 2, 3)));
static struct curl_slist *appendf(struct curl_slist *h, const char *fmt, const char *a) {
  char buf[256];
  if (snprintf(buf, sizeof buf, fmt, a) >= (int)sizeof buf) return h; /* too long: skip */
  return curl_slist_append(h, buf);
}

static struct curl_slist *build_headers(const char *token) {
  struct curl_slist *h = NULL;
  h = curl_slist_append(h, "Accept: application/json");
  h = appendf(h, "X-Plex-Platform: %s", PP_PLATFORM);
  h = curl_slist_append(h, "X-Plex-Platform-Version: 1.0");
  h = curl_slist_append(h, "X-Plex-Provides: controller");
  h = appendf(h, "X-Plex-Product: %s", PP_PRODUCT);
  h = appendf(h, "X-Plex-Version: %s", PP_VERSION);
  h = appendf(h, "X-Plex-Device: %s", PP_PRODUCT);
  h = appendf(h, "X-Plex-Device-Name: %s", PP_PRODUCT);
  h = appendf(h, "X-Plex-Client-Identifier: %s", g_client_id);
  h = curl_slist_append(h, "X-Plex-Language: en");
  if (token && token[0]) {
    /* tokens can exceed any fixed buffer: build this one dynamically */
    size_t need = strlen("X-Plex-Token: ") + strlen(token) + 1;
    char *buf = malloc(need);
    if (buf) {
      snprintf(buf, need, "X-Plex-Token: %s", token);
      h = curl_slist_append(h, buf);
      free(buf);
    }
  }
  return h;
}

static int http_run(const char *url, const char *token, const char *method,
                    int map_status_errors, pp_http_response *out) {
  if (!url || !out) return PP_ERR_ARG;
  memset(out, 0, sizeof *out);

  CURL *curl = curl_easy_init();
  if (!curl) return PP_ERR_NOMEM;
  struct curl_slist *headers = build_headers(token);

  char errbuf[CURL_ERROR_SIZE] = "";
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
  /* redirects stay OFF: with a custom X-Plex-Token header curl would forward
   * the token to whatever host the redirect points at */
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, PP_PRODUCT "/" PP_VERSION);
  if (method && strcmp(method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
  }

  CURLcode cc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (cc != CURLE_OK) {
    snprintf(out->err, sizeof out->err, "%s", errbuf[0] ? errbuf : curl_easy_strerror(cc));
    free(out->body);
    out->body = NULL;
    out->size = 0;
    out->status = 0;
    return PP_ERR_NET;
  }
  out->status = status;
  if (!map_status_errors) return PP_OK; /* caller inspects out->status */

  if (status == 401 || status == 403) { pp_http_free(out); return PP_ERR_AUTH; }
  if (status < 200 || status >= 300) {
    LOGW("http %ld from %s %s", status, method ? method : "GET", url);
    pp_http_free(out);
    return PP_ERR_HTTP;
  }
  return PP_OK;
}

int pp_http_get(const char *url, const char *token, pp_http_response *out) {
  return http_run(url, token, "GET", 1, out);
}

int pp_http_post(const char *url, const char *token, pp_http_response *out) {
  return http_run(url, token, "POST", 1, out);
}

int pp_http_get_status(const char *url, const char *token, pp_http_response *out) {
  return http_run(url, token, "GET", 0, out);
}

void pp_http_free(pp_http_response *r) {
  if (!r) return;
  free(r->body);
  r->body = NULL;
  r->size = 0;
}

const char *pp_internal_client_id(void) { return g_client_id; }

int pp_init(const char *client_id) {
  if (!client_id || !client_id[0]) return PP_ERR_ARG;
  snprintf(g_client_id, sizeof g_client_id, "%s", client_id);
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return PP_ERR_NET;
  return PP_OK;
}

void pp_cleanup(void) {
  curl_global_cleanup();
  g_client_id[0] = '\0';
}
