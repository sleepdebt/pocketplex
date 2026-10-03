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

/* ---- playback.c: pure building ---- */

/* Builds the universal-transcode start URL (HLS). path is the item key,
 * e.g. "/library/metadata/1234". offset_ms is passed to PMS as whole seconds
 * (PMS returns a full-length VOD playlist; callers normally pass 0 and give
 * the resume point to the player instead). */
int pp_build_transcode_url(const pp_server *srv, const char *path,
                           const char *session_id, int width, int height,
                           int max_kbps, long offset_ms, char *url_out, size_t n);

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
void pp_http_free(pp_http_response *r);

/* The client identifier pp_init() was given ("" before pp_init). */
const char *pp_internal_client_id(void);

#endif /* PP_PLEX_INTERNAL_H */
