/* plex/internal.h: core internal helpers. Not part of the frozen contract.
 *
 * The parse/build functions here are pure (no network) so tests can drive them
 * from recorded JSON under tests/fixtures. http.c/auth.c/library.c/playback.c
 * use them.
 */
#ifndef PP_PLEX_INTERNAL_H
#define PP_PLEX_INTERNAL_H

#include <stddef.h>
#include "plex/plex.h"

/* Client identity used in X-Plex-* headers (http.c) AND in the transcode
 * query (playback.c): players like mpv fetch start.m3u8 with no headers, and
 * without these in the URL PMS serves its lowest quality rung. */
#define PP_PRODUCT   "PocketPlex"
#define PP_VERSION   "0.1"
#define PP_PLATFORM  "Chrome" /* a transcode-profile name PMS knows; "Linux" -> 400 */

/* ---- auth.c: pure parsing ---- */

/* plex.tv/api/v2/pins JSON. Fills code (8 bytes), id, and token_out when the
 * PIN has been authorized (left empty while pending). 0 = ok, <0 = PP_ERR_*. */
int pp_parse_pin(const char *json, char code_out[8], long *id_out,
                 char *token_out, size_t token_n);

/* plex.tv/api/v2/resources JSON -> servers that provide "server".
 * url = the local=true connection if present, else the first connection.
 * token = the resource accessToken; client_id = the resource clientIdentifier. */
int pp_parse_servers(const char *json, pp_server **out, int *count);

/* ---- library.c: pure parsing ---- */

/* Any MediaContainer response whose items live under "Directory" (sections)
 * or "Metadata" (section contents, children, onDeck). out is zeroed on error. */
int pp_parse_items(const char *json, pp_list *out);

/* Fetches one metadata item by ratingKey (GET /library/metadata/{rk}).
 * Not part of the frozen contract; pp-cli needs the duration for timelines. */
int pp_fetch_item(const pp_server *srv, const char *rating_key, pp_item *out);

/* ---- playback.c: pure building / parsing ---- */

/* The universal-transcode query shared by decision and start (one builder so
 * the pre-flight check matches the real request). path is the item key,
 * e.g. "/library/metadata/1234". offset_ms is passed to PMS as whole seconds
 * (PMS returns a full-length VOD playlist; callers normally pass 0 and give
 * the resume point to the player instead). burn_subtitles 1 = subtitles=burn
 * (verified), 0 = subtitles=none (PlexKodiConnect's documented no-burn value,
 * accepted by PMS 1.43.3: see tests/fixtures/decision_subtitles_none.json). */
int pp_build_transcode_url(const pp_server *srv, const char *path,
                           const char *session_id, int width, int height,
                           int max_kbps, long offset_ms, int burn_subtitles,
                           char *url_out, size_t n);
int pp_build_decision_url(const pp_server *srv, const char *path,
                          const char *session_id, int width, int height,
                          int max_kbps, long offset_ms, int burn_subtitles,
                          char *url_out, size_t n);

/* Decision response: PP_OK when the server will transcode; PP_ERR_HTTP when
 * it refused (generalDecisionCode or transcodeDecisionCode >= 2000);
 * PP_ERR_PARSE on unusable JSON. */
int pp_parse_decision(const char *json);

/* ---- http.c ---- */

typedef struct {
  long status;    /* HTTP status; 0 = transport-level failure */
  char *body;     /* malloc'd NUL-terminated; NULL on transport failure */
  size_t size;
  char err[256];  /* curl error string when status == 0 */
} pp_http_response;

/* Sends Accept: application/json plus the standard X-Plex-* headers for this
 * client (set up by pp_init); X-Plex-Token only when token is non-NULL.
 * Returns PP_OK on 2xx, PP_ERR_AUTH on 401/403, PP_ERR_HTTP on other
 * non-2xx, PP_ERR_NET on connect/timeout/TLS failure. Free with pp_http_free. */
int pp_http_get(const char *url, const char *token, pp_http_response *out);
int pp_http_post(const char *url, const char *token, pp_http_response *out);
/* Like pp_http_get, but returns 0 for ANY completed HTTP exchange (status in
 * out->status; caller frees out), so callers can treat specific statuses
 * (404 on a stop, 404 on an expired PIN) themselves. PP_ERR_NET still means
 * the request never completed. */
int pp_http_get_status(const char *url, const char *token, pp_http_response *out);
void pp_http_free(pp_http_response *r);

/* The client identifier pp_init() was given ("" before pp_init). */
const char *pp_internal_client_id(void);

#endif /* PP_PLEX_INTERNAL_H */
