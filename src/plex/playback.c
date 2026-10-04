/* plex/playback.c: transcode URL building, session stop, timeline, scrobble.
 *
 * Verified against the owner's PMS 1.43.3 (see tests/fixtures + the spec):
 *  - the transcode endpoints need a client profile the server knows:
 *    X-Plex-Platform: Chrome. "Linux" gets HTTP 400.
 *  - PMS returns a full-length VOD playlist; offset only shifts segments, it
 *    does not reset position numbering, so callers pass offset 0 and hand the
 *    resume point to the player instead.
 */
#include "plex/internal.h"
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

/* Writes the formatted string only when it fits; url_out is left untouched
 * (and untouched means empty on first use) when it doesn't. */
static int format_if_fits(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int needed = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (needed < 0 || (size_t)needed >= n) return PP_ERR_ARG;
  va_start(ap, fmt);
  vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return 0;
}

/* The query shared by decision and start, so the pre-flight check matches
 * the request the player will actually make. */
static int build_transcode_query(const char *path, const char *session_id,
                                 int width, int height, int max_kbps,
                                 long offset_ms, int burn_subtitles,
                                 char *out, size_t n) {
  return format_if_fits(out, n,
      "?path=%s"
      "&mediaIndex=0&partIndex=0"
      "&protocol=hls&fastSeek=1&copyts=1"
      "&offset=%ld"
      "&maxVideoBitrate=%d"
      "&videoResolution=%dx%d"
      "&X-Plex-Platform=Chrome"
      "&directPlay=0&directStream=0"
      "&subtitles=%s"
      "&session=%s",
      path, offset_ms / 1000, max_kbps, width, height,
      burn_subtitles ? "burn" : "none", session_id);
}

int pp_build_transcode_url(const pp_server *srv, const char *path,
                           const char *session_id, int width, int height,
                           int max_kbps, long offset_ms, int burn_subtitles,
                           char *url_out, size_t n) {
  if (!srv || !srv->url || !path || !session_id || !url_out || !srv->token)
    return PP_ERR_ARG;
  char query[512];
  int rc = build_transcode_query(path, session_id, width, height, max_kbps,
                                 offset_ms, burn_subtitles, query, sizeof query);
  if (rc != PP_OK) return rc;
  return format_if_fits(url_out, n,
                        "%s/video/:/transcode/universal/start.m3u8%s&X-Plex-Token=%s",
                        srv->url, query, srv->token);
}

int pp_build_decision_url(const pp_server *srv, const char *path,
                          const char *session_id, int width, int height,
                          int max_kbps, long offset_ms, int burn_subtitles,
                          char *url_out, size_t n) {
  if (!srv || !srv->url || !path || !session_id || !url_out)
    return PP_ERR_ARG;
  char query[512];
  int rc = build_transcode_query(path, session_id, width, height, max_kbps,
                                 offset_ms, burn_subtitles, query, sizeof query);
  if (rc != PP_OK) return rc;
  return format_if_fits(url_out, n, "%s/video/:/transcode/universal/decision%s",
                        srv->url, query);
}

int pp_parse_decision(const char *json) {
  if (!json) return PP_ERR_ARG;
  cJSON *root = cJSON_Parse(json);
  if (!root) return PP_ERR_PARSE;
  cJSON *mc = cJSON_GetObjectItemCaseSensitive(root, "MediaContainer");
  if (!mc) { cJSON_Delete(root); return PP_ERR_PARSE; }
  int refused = 0;
  const char *fields[] = {"generalDecisionCode", "transcodeDecisionCode"};
  for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(mc, fields[i]);
    if (cJSON_IsNumber(code) && code->valuedouble >= 2000) refused = 1;
  }
  cJSON_Delete(root);
  return refused ? PP_ERR_HTTP : PP_OK;
}

/* Asks the server to validate the transcode decision so errors surface here,
 * before a player process is launched; also parses the refusal codes PMS
 * reports with HTTP 200. */
static int transcode_decision(const pp_server *srv, const char *path,
                              const char *session_id, int width, int height,
                              int max_kbps, long offset_ms, int burn_subtitles) {
  char url[1024];
  int rc = pp_build_decision_url(srv, path, session_id, width, height, max_kbps,
                                 offset_ms, burn_subtitles, url, sizeof url);
  if (rc != PP_OK) return rc;
  pp_http_response r;
  rc = pp_http_get(url, srv->token, &r);
  if (rc != PP_OK) { pp_http_free(&r); return rc; }
  rc = pp_parse_decision(r.body);
  pp_http_free(&r);
  return rc;
}

int pp_transcode_url_ex(const pp_server *srv, const pp_item *item,
                        const char *session_id, int width, int height,
                        int max_kbps, long offset_ms, int burn_subtitles,
                        char *url_out, size_t n) {
  if (!srv || !srv->url || !srv->token) return PP_ERR_ARG;
  if (!item || item->rating_key[0] == '\0') return PP_ERR_ARG;
  char path[64];
  snprintf(path, sizeof path, "/library/metadata/%s", item->rating_key);

  int rc = transcode_decision(srv, path, session_id, width, height, max_kbps,
                              offset_ms, burn_subtitles);
  if (rc != PP_OK) return rc;

  return pp_build_transcode_url(srv, path, session_id, width, height, max_kbps,
                                offset_ms, burn_subtitles, url_out, n);
}

int pp_transcode_url(const pp_server *srv, const pp_item *item, const char *session_id,
                     int width, int height, int max_kbps, long offset_ms,
                     char *url_out, size_t n) {
  return pp_transcode_url_ex(srv, item, session_id, width, height, max_kbps,
                             offset_ms, 1 /* owner Q4: subtitles on by default */,
                             url_out, n);
}

int pp_transcode_stop(const pp_server *srv, const char *session_id) {
  if (!srv || !srv->url || !srv->token) return PP_ERR_ARG;
  if (!session_id || !session_id[0]) return PP_ERR_ARG;
  char url[512];
  int written = snprintf(url, sizeof url, "%s/video/:/transcode/universal/stop?session=%s",
                         srv->url, session_id);
  if (written < 0 || (size_t)written >= sizeof url) return PP_ERR_ARG;
  pp_http_response r;
  int rc = pp_http_get_status(url, srv->token, &r);
  if (rc != PP_OK) { pp_http_free(&r); return rc; }
  long status = r.status;
  pp_http_free(&r);
  if (status == 404) return PP_OK; /* already gone / never started: idempotent */
  if (status == 401 || status == 403) return PP_ERR_AUTH;
  if (status < 200 || status >= 300) return PP_ERR_HTTP;
  return PP_OK;
}

int pp_timeline(const pp_server *srv, const pp_item *item, const char *state,
                long time_ms) {
  if (!srv || !srv->url || !srv->token) return PP_ERR_ARG;
  if (!item || !state) return PP_ERR_ARG;
  if (strcmp(state, "playing") != 0 && strcmp(state, "paused") != 0 &&
      strcmp(state, "stopped") != 0)
    return PP_ERR_ARG;
  /* duration is required by PMS 1.43.3 (400 without it) */
  if (item->duration_ms <= 0) return PP_ERR_ARG;
  char url[1024];
  int written = snprintf(url, sizeof url,
      "%s/:/timeline?ratingKey=%s&key=/library/metadata/%s"
      "&identifier=com.plexapp.plugins.library&time=%ld&state=%s&duration=%ld",
      srv->url, item->rating_key, item->rating_key, time_ms, state, item->duration_ms);
  if (written < 0 || (size_t)written >= sizeof url) return PP_ERR_ARG;
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  pp_http_free(&r);
  return rc;
}

int pp_scrobble(const pp_server *srv, const pp_item *item) {
  if (!srv || !srv->url || !srv->token) return PP_ERR_ARG;
  if (!item || item->rating_key[0] == '\0') return PP_ERR_ARG;
  char url[512];
  int written = snprintf(url, sizeof url,
      "%s/:/scrobble?key=%s&identifier=com.plexapp.plugins.library",
      srv->url, item->rating_key);
  if (written < 0 || (size_t)written >= sizeof url) return PP_ERR_ARG;
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  pp_http_free(&r);
  return rc;
}
