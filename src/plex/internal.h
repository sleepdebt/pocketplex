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
 * url = the best-ranked connection (see pp_rank_conns) without probing.
 * token = the resource accessToken; client_id = the resource clientIdentifier. */
int pp_parse_servers(const char *json, pp_server **out, int *count);

/* ---- auth.c: discovery model (connection ranking for shared servers) ---- */

/* How a connection was classified when it was chosen. */
enum { PP_CONN_LOCAL = 0, PP_CONN_REMOTE = 1, PP_CONN_RELAY = 2 };

#define PP_MAX_CONNS 8
#define PP_MAX_RESOURCES 16
#define PP_PROBE_BUDGET_MS 8000 /* worst-case total probe time per server */

/* One connections[] entry of a resource: where to reach a PMS and how plex.tv
 * classifies it. Fixed-size so resource parsing stays allocation-free. */
typedef struct {
  char uri[512];
  int local; /* resource says we're on the server's LAN */
  int relay; /* plex.tv relay (plex.direct:8443 style) */
} pp_conn;

/* A server resource from plex.tv/api/v2/resources, pre-allocation. */
typedef struct {
  char name[256];
  char token[256]; /* resource accessToken: the PMS token, NOT the account token */
  char client_id[80];
  int owned;
  int public_address_matches; /* client's public IP == server's: local conns plausible */
  pp_conn conns[PP_MAX_CONNS];
  int conn_count;
} pp_resource;

/* Reachability probe: return 1 when url answers GET /identity with 200 using
 * token in the X-Plex-Token header. Injected so tests never touch the network. */
typedef int (*pp_probe_fn)(const char *url, const char *token, void *ud);

/* Pure: resources JSON (same array pp_parse_servers reads) into res[0..max-1].
 * Resources that don't provide "server" are skipped; excess servers/conns are
 * truncated, not overflowed. 0 = ok, <0 = PP_ERR_*. */
int pp_parse_resources(const char *json, pp_resource *res, int max, int *count);

/* Pure: connection indices of res ordered best-first:
 *  1. local, but only when res->owned || res->public_address_matches
 *  2. remote direct (local=0, relay=0)
 *  3. relay
 *  4. local that is not allowed (a shared server's LAN address we can't route
 *     to) - kept as a display-only last resort.
 * Returns the number of indices written (== res->conn_count). */
int pp_rank_conns(const pp_resource *res, int order[PP_MAX_CONNS]);

/* Probes the ranked candidates in order and returns the index of the first
 * that answers 200. When none does (or the budget_ms runs out), returns the
 * best-ranked index anyway with *reachable_out = 0, so callers can still show
 * the server. Both out params are always written (class from the chosen
 * connection; PP_CONN_REMOTE when there is none). -1 only when res has
 * no connections or is NULL. */
int pp_choose_conn(const pp_resource *res, pp_probe_fn probe, void *ud,
                   long budget_ms, int *reachable_out, int *class_out);

/* pp_server plus what discovery learned about the chosen connection. */
typedef struct {
  pp_server server;
  int conn_class; /* PP_CONN_* of the chosen connection */
  int reachable;  /* 1 when the probe got a 200 */
} pp_server_info;

/* Discovery with ranking + probing (the network probe from http.c). The
 * pp_server_infos array is owned by the caller; free with the *_free below.
 * This is what pp_discover_servers (the frozen contract) calls internally. */
int pp_discover_servers_ex(const char *token, pp_probe_fn probe, void *ud,
                           pp_server_info **out, int *count);
void pp_server_infos_free(pp_server_info *servers, int count);

/* http.c: the real probe - GET {url}/identity, connect timeout 3 s, total 4 s,
 * token in a header, nothing logged. Returns 1 only on HTTP 200. */
int pp_http_probe(const char *url, const char *token, void *ud);

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
