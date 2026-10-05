/* plex/plex.h: Plex client contract.
 * Implemented by core (src/plex/). Change requests go to the spec.
 *
 * Conventions:
 *  - Functions returning int: 0 = ok, <0 = a pp_err code. pp_auth_pin_poll also returns 1 = pending.
 *  - Calls block on the network. The UI must call them from a worker thread.
 *  - Outputs (pp_list, pp_server arrays) are owned by the caller and freed with the matching *_free.
 *  - Never log tokens; log.h redacts "...token=" / "...Token": values, but don't rely on it.
 */
#ifndef PP_PLEX_H
#define PP_PLEX_H

#include <stddef.h>

typedef enum {
  PP_OK        =  0,
  PP_ERR_NET   = -1, /* connect/timeout/TLS failure: retryable */
  PP_ERR_AUTH  = -2, /* HTTP 401/403: token missing, expired or revoked */
  PP_ERR_HTTP  = -3, /* any other non-2xx status */
  PP_ERR_PARSE = -4, /* unexpected JSON */
  PP_ERR_ARG   = -5, /* bad argument or output buffer too small */
  PP_ERR_NOMEM = -6
} pp_err;

/* name: the server's friendly name from discovery (e.g. "Living Room"); NULL when unknown (e.g. dev fallback).
 * Added 2026-10-04; freed by pp_servers_free. */
typedef struct { char *url; char *token; char *client_id; char *name; } pp_server;

typedef enum { PP_SECTION, PP_SHOW, PP_SEASON, PP_EPISODE, PP_MOVIE,
               PP_ARTIST, PP_ALBUM, PP_TRACK, PP_DIR } pp_kind;

typedef struct {
  char rating_key[32]; char key[256]; char title[256]; char subtitle[256];
  pp_kind kind; int year; long duration_ms; long view_offset_ms;
  int watched; int leaf_count; int viewed_leaf_count;
  char summary[512]; /* truncated; shown on the Item detail screen */
} pp_item;

typedef struct { pp_item *items; int count; } pp_list;

/* Call once before any other pp_* function (sets X-Plex-Client-Identifier, inits libcurl). */
int  pp_init(const char *client_id);
void pp_cleanup(void);

/* PIN login: show code_out to the user (plex.tv/link), then poll about every 2 s. */
int  pp_auth_pin_start(char code_out[8], long *pin_id_out);
int  pp_auth_pin_poll(long pin_id, char *token_out, size_t n); /* 0=ok, 1=pending, <0=err */

int  pp_discover_servers(const char *token, pp_server **out, int *count);
void pp_servers_free(pp_server *servers, int count);

int  pp_sections(const pp_server*, pp_list *out);
int  pp_children(const pp_server*, const char *key, pp_list *out);
int  pp_on_deck(const pp_server*, pp_list *out);

int  pp_transcode_url(const pp_server*, const pp_item*, const char *session_id,
                      int width, int height, int max_kbps, long offset_ms,
                      char *url_out, size_t n);
/* Same as pp_transcode_url, which is pp_transcode_url_ex(..., burn_subtitles = 1, ...).
 * burn_subtitles = 0 asks PMS for no subtitles. (Added 2026-10-04, owner decision on Q4.) */
int  pp_transcode_url_ex(const pp_server*, const pp_item*, const char *session_id,
                         int width, int height, int max_kbps, long offset_ms,
                         int burn_subtitles, char *url_out, size_t n);
/* Ends the PMS transcode for session_id. Call on player exit (and from an atexit handler). */
int  pp_transcode_stop(const pp_server*, const char *session_id);

int  pp_timeline(const pp_server*, const pp_item*, const char *state /*playing|paused|stopped*/, long time_ms);
int  pp_scrobble(const pp_server*, const pp_item*);

void pp_list_free(pp_list*);

#endif /* PP_PLEX_H */
