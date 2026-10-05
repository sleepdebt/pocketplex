/* worker.c: async worker thread for provider calls.
 * Runs plex.h / fake_provider calls on a detached pthread so the UI never
 * blocks >50 ms. See worker.h for the ownership rules.
 */
#include "ui/worker.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct pp_request {
  /* Inputs: owned copies, written before the thread starts, then read-only. */
  pp_req_type type;
  int         use_fake;
  pp_server   srv;          /* deep copy (strings owned) */
  int         has_srv;
  char       *key;
  char       *token;
  pp_item    *item;         /* playback requests */
  char       *session;
  char        state[16];
  long        time_ms;
  int         max_kbps;
  int         burn_subtitles;

  /* Outputs: written only by the worker, read by the caller after done. */
  int        status;
  char       error[128];
  pp_list    result;
  pp_server *servers;
  int        server_count;
  char       pin[8];
  long       pin_id;        /* input for PIN_POLL, output for PIN_START */
  char       auth_token[256];
  char       url[2048];     /* REQ_TRANSCODE_URL: contains the token */

  /* Synchronisation (atomic builtins). */
  int done;                 /* 1 once outputs are final (release store) */
  int cancelled;            /* caller released early; skip remaining work */
  int refs;                 /* caller + worker; last one frees */
};

static int g_live = 0;      /* live requests, for shutdown and tests */
static int g_ran = 0;       /* requests whose provider call actually ran */

static void sleep_ms(int ms) {
  struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
  while (nanosleep(&ts, &ts) != 0) {}
}

/* Simulated network latency for smoke runs and tests. */
static int fake_delay_ms(void) {
  const char *s = getenv("PP_FAKE_DELAY_MS");
  if (!s) return 0;
  int v = atoi(s);
  return v > 0 ? v : 0;
}

static void set_error(pp_request *req, const char *msg) {
  snprintf(req->error, sizeof(req->error), "%s", msg);
}

static char *dup_or_null(const char *s) {
  return s ? strdup(s) : NULL;
}

static void request_free(pp_request *req) {
  pp_list_free(&req->result);
  pp_servers_free(req->servers, req->server_count);
  free(req->srv.url);
  free(req->srv.token);
  free(req->srv.client_id);
  free(req->key);
  free(req->token);
  free(req->item);
  free(req->session);
  /* Don't leave tokens lying around in freed heap. */
  memset(req->auth_token, 0, sizeof(req->auth_token));
  memset(req->url, 0, sizeof(req->url));
  free(req);
  __atomic_sub_fetch(&g_live, 1, __ATOMIC_ACQ_REL);
}

static void request_unref(pp_request *req) {
  if (__atomic_sub_fetch(&req->refs, 1, __ATOMIC_ACQ_REL) == 0) request_free(req);
}

static int is_cancelled(const pp_request *req) {
  return __atomic_load_n(&req->cancelled, __ATOMIC_ACQUIRE);
}

static void run_fake(pp_request *req) {
  switch (req->type) {
  case REQ_SERVERS:   req->status = fake_servers(&req->servers, &req->server_count); break;
  case REQ_SECTIONS:  req->status = fake_sections(NULL, &req->result); break;
  case REQ_CHILDREN:  req->status = fake_children(NULL, req->key, &req->result); break;
  case REQ_ON_DECK:   req->status = fake_on_deck(NULL, &req->result); break;
  case REQ_PIN_START:
    snprintf(req->pin, sizeof(req->pin), "%04d", (int)(time(NULL) % 10000));
    req->pin_id = 12345;
    req->status = PP_OK;
    break;
  case REQ_PIN_POLL:  req->status = 1; break;  /* always pending in smoke mode */
  case REQ_TRANSCODE_URL:
    snprintf(req->url, sizeof(req->url), "http://fake.invalid/%s?subtitles=%s&session=%s",
             req->item ? req->item->rating_key : "", req->burn_subtitles ? "burn" : "none",
             req->session ? req->session : "");
    req->status = PP_OK;
    break;
  case REQ_TIMELINE:
  case REQ_SCROBBLE:
  case REQ_PLAY_STOP: req->status = PP_OK; break;
  default:            req->status = PP_ERR_ARG; break;
  }
}

static void run_real(pp_request *req) {
  const pp_server *srv = req->has_srv ? &req->srv : NULL;
  switch (req->type) {
  case REQ_SERVERS:   req->status = pp_discover_servers(req->token, &req->servers, &req->server_count); break;
  case REQ_SECTIONS:  req->status = pp_sections(srv, &req->result); break;
  case REQ_CHILDREN:  req->status = pp_children(srv, req->key, &req->result); break;
  case REQ_ON_DECK:   req->status = pp_on_deck(srv, &req->result); break;
  case REQ_PIN_START: req->status = pp_auth_pin_start(req->pin, &req->pin_id); break;
  case REQ_PIN_POLL:  req->status = pp_auth_pin_poll(req->pin_id, req->auth_token, sizeof(req->auth_token)); break;
  case REQ_TRANSCODE_URL:
    req->status = pp_transcode_url_ex(srv, req->item, req->session, 640, 480, req->max_kbps,
                                      0, req->burn_subtitles, req->url, sizeof(req->url));
    if (req->status != PP_OK) memset(req->url, 0, sizeof(req->url));
    break;
  case REQ_TIMELINE:  req->status = pp_timeline(srv, req->item, req->state, req->time_ms); break;
  case REQ_SCROBBLE:  req->status = pp_scrobble(srv, req->item); break;
  case REQ_PLAY_STOP: {
    /* Report the final position first, then end the transcode regardless. */
    int tl = pp_timeline(srv, req->item, req->state, req->time_ms);
    int ts = pp_transcode_stop(srv, req->session);
    req->status = tl != PP_OK ? tl : ts;
    break;
  }
  default:            req->status = PP_ERR_ARG; break;
  }
}

static void *worker_thread(void *arg) {
  pp_request *req = (pp_request *)arg;

  int delay = fake_delay_ms();
  if (delay > 0 && !is_cancelled(req)) sleep_ms(delay);

  if (is_cancelled(req)) {
    req->status = PP_ERR_ARG;
  } else {
    if (req->use_fake) run_fake(req);
    else run_real(req);
    __atomic_add_fetch(&g_ran, 1, __ATOMIC_ACQ_REL);
  }

  if (req->status != PP_OK && req->status != 1) {
    switch (req->status) {
    case PP_ERR_AUTH:  set_error(req, "Authentication failed (relink)"); break;
    case PP_ERR_NET:   set_error(req, "Network error (retry)"); break;
    case PP_ERR_HTTP:  set_error(req, "Server error (retry)"); break;
    case PP_ERR_PARSE: set_error(req, "Parse error"); break;
    case PP_ERR_NOMEM: set_error(req, "Out of memory"); break;
    default:           set_error(req, "Request failed"); break;
    }
  }
  if (req->type >= REQ_TRANSCODE_URL && !req->use_fake && !is_cancelled(req)) {
    if (req->status == PP_OK) LOGD("play request %d ok", (int)req->type);
    else LOGW("play request %d failed: %d", (int)req->type, req->status);
  }
  __atomic_store_n(&req->done, 1, __ATOMIC_RELEASE);
  request_unref(req);  /* may free req if the caller already released */
  return NULL;
}

static void copy_server(pp_request *req, const pp_server *srv, int *oom) {
  if (!srv) return;
  req->has_srv = 1;
  req->srv.url = dup_or_null(srv->url);
  req->srv.token = dup_or_null(srv->token);
  req->srv.client_id = dup_or_null(srv->client_id);
  *oom |= (srv->url && !req->srv.url) || (srv->token && !req->srv.token) ||
          (srv->client_id && !req->srv.client_id);
}

static pp_request *request_alloc(pp_req_type type, int use_fake) {
  pp_request *req = (pp_request *)calloc(1, sizeof(*req));
  if (!req) return NULL;
  __atomic_add_fetch(&g_live, 1, __ATOMIC_ACQ_REL);
  req->type = type;
  req->use_fake = use_fake;
  req->refs = 2;
  return req;
}

/* Start the thread, or finish the request immediately as an error. */
static pp_request *request_launch(pp_request *req, int oom) {
  pthread_t t;
  pthread_attr_t attr;
  int started = 0;
  if (!oom && req->type != REQ_NONE && pthread_attr_init(&attr) == 0) {
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    started = pthread_create(&t, &attr, worker_thread, req) == 0;
    pthread_attr_destroy(&attr);
  }
  if (!started) {
    req->status = req->type == REQ_NONE ? PP_ERR_ARG : PP_ERR_NOMEM;
    set_error(req, req->type == REQ_NONE ? "Request failed" : "Could not start request");
    req->done = 1;
    req->refs = 1;  /* no worker reference */
  }
  return req;
}

pp_request *worker_start(pp_req_type type, const pp_server *srv, const char *key,
                         const char *token, long pin_id, int use_fake) {
  pp_request *req = request_alloc(type, use_fake);
  if (!req) return NULL;
  int oom = 0;
  req->pin_id = pin_id;
  copy_server(req, srv, &oom);
  req->key = dup_or_null(key);
  req->token = dup_or_null(token);
  oom |= (key && !req->key) || (token && !req->token);
  return request_launch(req, oom);
}

pp_request *worker_start_play(pp_req_type type, const pp_server *srv,
                              const pp_play_args *args, int use_fake) {
  pp_request *req = request_alloc(type, use_fake);
  if (!req) return NULL;
  int oom = 0;
  copy_server(req, srv, &oom);
  if (args) {
    if (args->item) {
      req->item = (pp_item *)malloc(sizeof(pp_item));
      if (req->item) *req->item = *args->item; else oom = 1;
    }
    req->session = dup_or_null(args->session);
    oom |= args->session && !req->session;
    if (args->state) snprintf(req->state, sizeof(req->state), "%s", args->state);
    req->time_ms = args->time_ms;
    req->max_kbps = args->max_kbps;
    req->burn_subtitles = args->burn_subtitles;
  }
  return request_launch(req, oom);
}

int worker_is_done(const pp_request *req) {
  if (!req) return 1;
  return __atomic_load_n(&req->done, __ATOMIC_ACQUIRE);
}

int worker_status(const pp_request *req) { return req ? req->status : PP_ERR_NOMEM; }
const char *worker_error(const pp_request *req) { return req ? req->error : "Out of memory"; }
const char *worker_pin(const pp_request *req) { return req ? req->pin : ""; }
long worker_pin_id(const pp_request *req) { return req ? req->pin_id : 0; }
const char *worker_auth_token(const pp_request *req) { return req ? req->auth_token : ""; }
const char *worker_url(const pp_request *req) { return req ? req->url : ""; }

void worker_take_list(pp_request *req, pp_list *out) {
  if (!out) return;
  if (!req) { out->items = NULL; out->count = 0; return; }
  *out = req->result;
  req->result.items = NULL;
  req->result.count = 0;
}

void worker_take_servers(pp_request *req, pp_server **out, int *count) {
  if (out) *out = req ? req->servers : NULL;
  if (count) *count = req ? req->server_count : 0;
  if (req) { req->servers = NULL; req->server_count = 0; }
}

void worker_release(pp_request *req) {
  if (!req) return;
  __atomic_store_n(&req->cancelled, 1, __ATOMIC_RELEASE);
  request_unref(req);
}

void worker_detach(pp_request *req) {
  if (req) request_unref(req);
}

int worker_ran_count(void) {
  return __atomic_load_n(&g_ran, __ATOMIC_ACQUIRE);
}

int worker_live_count(void) {
  return __atomic_load_n(&g_live, __ATOMIC_ACQUIRE);
}

int worker_wait_idle(int timeout_ms) {
  int waited = 0;
  while (worker_live_count() > 0) {
    if (waited >= timeout_ms) return -1;
    sleep_ms(5);
    waited += 5;
  }
  return 0;
}
