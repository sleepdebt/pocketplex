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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pp_build_transcode_url(const pp_server *srv, const char *path,
                           const char *session_id, int width, int height,
                           int max_kbps, long offset_ms, char *url_out, size_t n) {
  if (!srv || !srv->url || !path || !session_id || !url_out || !srv->token)
    return PP_ERR_ARG;
  int written = snprintf(url_out, n,
      "%s/video/:/transcode/universal/start.m3u8"
      "?path=%s"
      "&mediaIndex=0&partIndex=0"
      "&protocol=hls&fastSeek=1&copyts=1"
      "&offset=%ld"
      "&maxVideoBitrate=%d"
      "&videoResolution=%dx%d"
      "&X-Plex-Platform=Chrome"
      "&directPlay=0&directStream=0"
      "&subtitles=burn"
      "&session=%s"
      "&X-Plex-Token=%s",
      srv->url, path, offset_ms / 1000, max_kbps, width, height, session_id, srv->token);
  if (written < 0 || (size_t)written >= n) return PP_ERR_ARG;
  return 0;
}

/* Asks the server to validate the transcode decision so errors surface here,
 * before a player process is launched. Same params as the start URL. */
static int transcode_decision(const pp_server *srv, const char *path,
                              const char *session_id, int width, int height,
                              int max_kbps) {
  char url[1024];
  snprintf(url, sizeof url,
      "%s/video/:/transcode/universal/decision"
      "?path=%s&mediaIndex=0&partIndex=0&protocol=hls&fastSeek=1&copyts=1"
      "&offset=0&maxVideoBitrate=%d&videoResolution=%dx%d&X-Plex-Platform=Chrome"
      "&directPlay=0&directStream=0&session=%s",
      srv->url, path, max_kbps, width, height, session_id);
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  pp_http_free(&r);
  return rc;
}

int pp_transcode_url(const pp_server *srv, const pp_item *item, const char *session_id,
                     int width, int height, int max_kbps, long offset_ms,
                     char *url_out, size_t n) {
  if (!item || item->rating_key[0] == '\0') return PP_ERR_ARG;
  char path[64];
  snprintf(path, sizeof path, "/library/metadata/%s", item->rating_key);

  int rc = transcode_decision(srv, path, session_id, width, height, max_kbps);
  if (rc != PP_OK) return rc;

  return pp_build_transcode_url(srv, path, session_id, width, height, max_kbps,
                                offset_ms, url_out, n);
}

int pp_transcode_stop(const pp_server *srv, const char *session_id) {
  if (!srv || !session_id || !session_id[0]) return PP_ERR_ARG;
  char url[512];
  snprintf(url, sizeof url, "%s/video/:/transcode/universal/stop?session=%s",
           srv->url, session_id);
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  pp_http_free(&r);
  return rc;
}

int pp_timeline(const pp_server *srv, const pp_item *item, const char *state,
                long time_ms) {
  if (!srv || !item || !state) return PP_ERR_ARG;
  if (strcmp(state, "playing") != 0 && strcmp(state, "paused") != 0 &&
      strcmp(state, "stopped") != 0)
    return PP_ERR_ARG;
  /* duration is required by PMS 1.43.3 (400 without it) */
  if (item->duration_ms <= 0) return PP_ERR_ARG;
  char url[1024];
  snprintf(url, sizeof url,
      "%s/:/timeline?ratingKey=%s&key=/library/metadata/%s"
      "&identifier=com.plexapp.plugins.library&time=%ld&state=%s&duration=%ld",
      srv->url, item->rating_key, item->rating_key, time_ms, state, item->duration_ms);
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  pp_http_free(&r);
  return rc;
}

int pp_scrobble(const pp_server *srv, const pp_item *item) {
  if (!srv || !item || item->rating_key[0] == '\0') return PP_ERR_ARG;
  char url[512];
  snprintf(url, sizeof url,
      "%s/:/scrobble?key=%s&identifier=com.plexapp.plugins.library",
      srv->url, item->rating_key);
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  pp_http_free(&r);
  return rc;
}
