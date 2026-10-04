/* ui/worker.h: async worker thread for provider calls.
 * Routes fake_provider/plex calls off the main thread so the UI never
 * blocks >50 ms. No SDL here (CORE module), so it is unit-testable under ASan.
 *
 * Ownership: a request is a heap object with two references, one held by the
 * caller and one by the worker thread. The caller never frees it directly; it
 * calls worker_release(). Whoever drops the last reference frees the request
 * and any results that were not taken. So a screen can be destroyed at any
 * time, even with its worker still blocked inside curl: the worker keeps
 * writing into memory it still owns and frees it when it finishes.
 *
 * Inputs (server, key, token) are deep-copied at submit, so the worker never
 * reads UI state (e.g. the current server) that the UI may free or replace.
 */
#ifndef PP_WORKER_H
#define PP_WORKER_H

#include "plex/plex.h"

typedef enum {
  REQ_NONE,
  REQ_SERVERS,
  REQ_SECTIONS,
  REQ_CHILDREN,
  REQ_ON_DECK,
  REQ_PIN_START,
  REQ_PIN_POLL,
  REQ_TRANSCODE_URL,   /* pp_transcode_url (offset 0; resume goes to player_start) */
  REQ_TIMELINE,        /* pp_timeline(state, time_ms) */
  REQ_SCROBBLE,        /* pp_scrobble */
  REQ_PLAY_STOP,       /* pp_timeline(state, time_ms) then pp_transcode_stop */
} pp_req_type;

/* Inputs for playback requests; everything is copied at submit. */
typedef struct {
  const pp_item *item;
  const char *session;  /* transcode session id */
  const char *state;    /* "playing" | "paused" | "stopped" (timeline / stop) */
  long time_ms;         /* timeline position */
  int max_kbps;         /* transcode bitrate */
} pp_play_args;

typedef struct pp_request pp_request;

/* Start a request on a new detached thread. srv/key/token may be NULL and are
 * copied. pin_id is the input for REQ_PIN_POLL. use_fake = 1 dispatches to
 * fake_provider (smoke/tests). Returns NULL only if out of memory; a thread
 * creation failure returns a request that is already done with PP_ERR_NOMEM. */
pp_request *worker_start(pp_req_type type, const pp_server *srv, const char *key,
                         const char *token, long pin_id, int use_fake);

/* Start a playback request (REQ_TRANSCODE_URL .. REQ_PLAY_STOP). Same
 * ownership rules as worker_start; use_fake returns canned results. */
pp_request *worker_start_play(pp_req_type type, const pp_server *srv,
                              const pp_play_args *args, int use_fake);

/* 1 once the worker has finished writing (acquire). NULL counts as done. The
 * accessors below may only be called once this returns 1. */
int  worker_is_done(const pp_request *req);

int         worker_status(const pp_request *req);   /* PP_OK, <0 pp_err, 1 = PIN pending; NULL -> PP_ERR_NOMEM */
const char *worker_error(const pp_request *req);    /* "" if none */
const char *worker_pin(const pp_request *req);      /* REQ_PIN_START */
long        worker_pin_id(const pp_request *req);   /* REQ_PIN_START */
const char *worker_auth_token(const pp_request *req); /* REQ_PIN_POLL on success */
const char *worker_url(const pp_request *req);      /* REQ_TRANSCODE_URL on success; holds the token, never log it */

/* Move the result out of the request; the caller then owns it (pp_list_free /
 * pp_servers_free). Afterwards the request holds nothing. */
void worker_take_list(pp_request *req, pp_list *out);
void worker_take_servers(pp_request *req, pp_server **out, int *count);

/* Drop the caller's reference. Safe on NULL, and safe while the worker is
 * still running (it is told to skip remaining work and frees on exit). The
 * caller must not touch req afterwards. */
void worker_release(pp_request *req);

/* Number of live requests (allocated, not yet freed). */
int  worker_live_count(void);
/* Wait up to timeout_ms for every request to be freed. 0 = idle, -1 = timeout. */
int  worker_wait_idle(int timeout_ms);

#endif /* PP_WORKER_H */
