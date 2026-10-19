/* tests/test_ui_worker.c: worker request ownership.
 * The UI may release a request at any time, even while its worker thread is
 * still running; the worker must then free it, never the UI. Run under ASan
 * (see REPORT) to turn any write-after-free into a hard failure; the heap
 * canary check below catches it without ASan too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test.h"
#include "ui/worker.h"
#include "platform/wifi.h"
#include "plex/plex.h"

static void sleep_ms(int ms) {
  struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
  nanosleep(&ts, NULL);
}

static int wait_done(pp_request *req, int timeout_ms) {
  int waited = 0;
  while (!worker_is_done(req) && waited < timeout_ms) { sleep_ms(1); waited++; }
  return worker_is_done(req);
}

static void test_complete_and_take(void) {
  unsetenv("PP_FAKE_DELAY_MS");
  pp_request *req = worker_start(REQ_CHILDREN, NULL, "tv", NULL, 0, 1);
  CHECK(req != NULL);
  CHECK(wait_done(req, 5000));
  CHECK(worker_status(req) == PP_OK);
  pp_list out;
  worker_take_list(req, &out);
  CHECK(out.count == 2000);
  CHECK(out.items != NULL);
  pp_list again;
  worker_take_list(req, &again);           /* moved out: request is empty now */
  CHECK(again.items == NULL && again.count == 0);
  worker_release(req);
  CHECK(worker_wait_idle(1000) == 0);
  CHECK(out.items[1999].title[0] != '\0');  /* caller's copy outlives the request */
  pp_list_free(&out);
}

/* Released after done without taking: the release frees the 2000 results. */
static void test_release_untaken_results(void) {
  unsetenv("PP_FAKE_DELAY_MS");
  pp_request *req = worker_start(REQ_CHILDREN, NULL, "tv", NULL, 0, 1);
  CHECK(wait_done(req, 5000));
  worker_release(req);
  CHECK(worker_live_count() == 0);
}

/* The blocker: screen popped mid-request. */
static void test_cancel_mid_request_no_write_after_free(void) {
  setenv("PP_FAKE_DELAY_MS", "150", 1);
  pp_request *req = worker_start(REQ_CHILDREN, NULL, "tv", NULL, 0, 1);
  CHECK(req != NULL);
  CHECK(!worker_is_done(req));             /* worker is asleep in the fake delay */
  worker_release(req);                     /* UI drops it, as list_destroy does */
  req = NULL;
  CHECK(worker_live_count() == 1);         /* not freed: the worker still owns it */

  /* Churn the heap with canary-filled blocks of every nearby size. If the
   * request had been freed here, the allocator would reuse it and the
   * worker's late writes (status, error, done, refcount) would hit a canary. */
  enum { N = 256 };
  unsigned char *blocks[N];
  size_t sizes[N];
  for (int i = 0; i < N; i++) {
    sizes[i] = 64 + (size_t)i * 8;
    blocks[i] = malloc(sizes[i]);
    if (blocks[i]) memset(blocks[i], 0xA5, sizes[i]);
  }

  CHECK(worker_wait_idle(5000) == 0);      /* worker finished and freed it */

  int corrupt = 0;
  for (int i = 0; i < N; i++) {
    if (!blocks[i]) continue;
    for (size_t j = 0; j < sizes[i]; j++) if (blocks[i][j] != 0xA5) { corrupt++; break; }
    free(blocks[i]);
  }
  CHECK(corrupt == 0);
  unsetenv("PP_FAKE_DELAY_MS");
}

/* Many requests released at different points of their life. */
static void test_release_stress(void) {
  setenv("PP_FAKE_DELAY_MS", "20", 1);
  enum { N = 32 };
  pp_request *reqs[N];
  for (int i = 0; i < N; i++)
    reqs[i] = worker_start(i % 3 ? REQ_CHILDREN : REQ_SECTIONS, NULL, "tv", NULL, 0, 1);
  for (int i = 0; i < N; i += 2) worker_release(reqs[i]);  /* mid-flight */
  sleep_ms(60);
  for (int i = 1; i < N; i += 2) {                          /* after done */
    CHECK(wait_done(reqs[i], 5000));
    worker_release(reqs[i]);
  }
  CHECK(worker_wait_idle(5000) == 0);
  unsetenv("PP_FAKE_DELAY_MS");
}

/* Inputs are copied: the caller may free its server right after submit, as
 * ui_set_server does when the user picks another server mid-load. Uses the
 * real provider against a closed loopback port (fails fast, no network). */
static void test_server_is_copied(void) {
  unsetenv("PP_FAKE_DELAY_MS");
  pp_server *srv = calloc(1, sizeof(*srv));
  srv->url = strdup("http://127.0.0.1:1");
  srv->token = strdup("REDACTED");
  srv->client_id = strdup("test-client");
  srv->name = strdup("Living Room");
  char *key = strdup("/library/metadata/1/children");
  pp_request *req = worker_start(REQ_CHILDREN, srv, key, NULL, 0, 0);
  pp_servers_free(srv, 1);  /* frees name too; the request kept its own copy */
  free(key);
  CHECK(wait_done(req, 10000));
  CHECK(worker_status(req) < 0);
  CHECK(worker_error(req)[0] != '\0');
  worker_release(req);
  CHECK(worker_wait_idle(1000) == 0);
}

static void test_edge_cases(void) {
  pp_request *req = worker_start(REQ_NONE, NULL, NULL, NULL, 0, 1);
  CHECK(req != NULL);
  CHECK(worker_is_done(req));
  CHECK(worker_status(req) == PP_ERR_ARG);
  worker_release(req);
  worker_release(NULL);
  CHECK(worker_is_done(NULL));
  CHECK(worker_status(NULL) == PP_ERR_NOMEM);
  CHECK(worker_live_count() == 0);

  req = worker_start(REQ_PIN_START, NULL, NULL, NULL, 0, 1);
  CHECK(wait_done(req, 5000));
  CHECK(worker_status(req) == PP_OK);
  CHECK(strlen(worker_pin(req)) == 4);
  CHECK(worker_pin_id(req) == 12345);
  worker_release(req);
  CHECK(worker_wait_idle(1000) == 0);
}

/* Playback requests copy the item and session; fake mode never touches the network. */
static void test_play_requests_fake(void) {
  unsetenv("PP_FAKE_DELAY_MS");
  pp_item *it = calloc(1, sizeof(*it));
  snprintf(it->rating_key, sizeof(it->rating_key), "53835");
  char *session = strdup("0f0e0d0c-0b0a-4908-8706-050403020100");
  pp_play_args a = { it, session, NULL, 0, 1500, 800, 450, 1 };
  pp_request *url = worker_start_play(REQ_TRANSCODE_URL, NULL, &a, 1);
  a.state = "playing"; a.time_ms = 30000;
  pp_request *tl = worker_start_play(REQ_TIMELINE, NULL, &a, 1);
  pp_request *sc = worker_start_play(REQ_SCROBBLE, NULL, &a, 1);
  a.state = "stopped";
  pp_request *st = worker_start_play(REQ_PLAY_STOP, NULL, &a, 1);
  free(it);                                  /* caller's copies can go right away */
  free(session);
  CHECK(wait_done(url, 5000) && wait_done(tl, 5000) && wait_done(sc, 5000) && wait_done(st, 5000));
  CHECK(worker_status(url) == PP_OK);
  CHECK(strstr(worker_url(url), "53835") != NULL);
  CHECK(strstr(worker_url(url), "0f0e0d0c-0b0a-4908-8706-050403020100") != NULL);
  CHECK(worker_status(tl) == PP_OK);
  CHECK(worker_status(sc) == PP_OK);
  CHECK(worker_status(st) == PP_OK);
  CHECK(strstr(worker_url(url), "subtitles=burn") != NULL);
  CHECK(strstr(worker_url(url), "videoResolution=800x450") != NULL);  /* preset canvas */
  CHECK_STR(worker_url(NULL), "");
  worker_release(url); worker_release(tl); worker_release(sc); worker_release(st);
  CHECK(worker_wait_idle(1000) == 0);

  pp_item it2;
  memset(&it2, 0, sizeof it2);
  pp_play_args off = { &it2, "s", NULL, 0, 1500, 0, 0, 0 };   /* burn_subtitles = 0 */
  pp_request *u2 = worker_start_play(REQ_TRANSCODE_URL, NULL, &off, 1);
  CHECK(wait_done(u2, 5000));
  CHECK(strstr(worker_url(u2), "subtitles=none") != NULL);
  CHECK(strstr(worker_url(u2), "videoResolution=640x480") != NULL);  /* 0,0 -> default */
  worker_release(u2);
  CHECK(worker_wait_idle(1000) == 0);
}

/* Real provider, closed loopback port: errors cleanly, fire-and-forget release is safe. */
static void test_play_stop_released_in_flight(void) {
  pp_server srv = { "http://127.0.0.1:1", "REDACTED", "test-client", NULL };
  pp_item it;
  memset(&it, 0, sizeof it);
  snprintf(it.rating_key, sizeof it.rating_key, "1");
  snprintf(it.key, sizeof it.key, "/library/metadata/1");
  pp_play_args a = { &it, "session", "stopped", 1000, 1500, 0, 0, 1 };
  worker_detach(worker_start_play(REQ_PLAY_STOP, &srv, &a, 0));  /* as the player screen does */
  pp_request *r = worker_start_play(REQ_TRANSCODE_URL, &srv, &a, 0);
  CHECK(wait_done(r, 10000));
  CHECK(worker_status(r) < 0);
  CHECK_STR(worker_url(r), "");
  worker_release(r);
  CHECK(worker_wait_idle(10000) == 0);
}

/* Fire-and-forget (timeline, scrobble, stop): detach must still run the
 * request; release cancels it. The player screen relies on the difference. */
static void test_detach_runs_release_cancels(void) {
  setenv("PP_FAKE_DELAY_MS", "50", 1);
  pp_item it;
  memset(&it, 0, sizeof it);
  pp_play_args a = { &it, "s", "stopped", 1000, 1500, 0, 0, 1 };
  int before = worker_ran_count();
  worker_detach(worker_start_play(REQ_PLAY_STOP, NULL, &a, 1));
  CHECK(worker_wait_idle(5000) == 0);
  CHECK(worker_ran_count() == before + 1);   /* detached: ran to completion */
  worker_release(worker_start_play(REQ_PLAY_STOP, NULL, &a, 1));
  CHECK(worker_wait_idle(5000) == 0);
  CHECK(worker_ran_count() == before + 1);   /* released mid-delay: skipped */
  worker_detach(NULL);
  unsetenv("PP_FAKE_DELAY_MS");
}

/* REQ_UNSCROBBLE: fake succeeds; the real path errors cleanly on a dead server
 * (until ws-a's pp_unscrobble lands, a weak stub answers PP_ERR_NET — either
 * way the UI must see a clean failure, never a crash). */
static void test_unscrobble_fake_and_real_error(void) {
  unsetenv("PP_FAKE_DELAY_MS");
  pp_item it;
  memset(&it, 0, sizeof it);
  snprintf(it.rating_key, sizeof it.rating_key, "53835");
  pp_play_args a = { &it, NULL, NULL, 0, 0, 0, 0, 0 };
  pp_request *req = worker_start_play(REQ_UNSCROBBLE, NULL, &a, 1);
  CHECK(req != NULL);
  CHECK(wait_done(req, 5000));
  CHECK(worker_status(req) == PP_OK);
  worker_release(req);

  pp_server srv = { "http://127.0.0.1:1", "REDACTED", "test-client", NULL };
  req = worker_start_play(REQ_UNSCROBBLE, &srv, &a, 0);
  CHECK(wait_done(req, 10000));
  CHECK(worker_status(req) < 0);
  CHECK(worker_error(req)[0] != '\0');
  worker_release(req);
  CHECK(worker_wait_idle(1000) == 0);
}

/* After a resume, the next request waits for Wi-Fi (PP_RESUME_DELAY_MS) and
 * retries a network error once (PP_RESUME_RETRY_MS); the request after that
 * is back to normal. */
static void test_resume_retry_once(void) {
  setenv("PP_RESUME_DELAY_MS", "1", 1);
  setenv("PP_RESUME_RETRY_MS", "1", 1);
  CHECK(worker_resume_pending() == 0);
  worker_note_resume();
  CHECK(worker_resume_pending() == 1);

  int ran = worker_ran_count();
  pp_server srv = { "http://127.0.0.1:1", "REDACTED", "test-client", NULL };
  pp_request *req = worker_start(REQ_CHILDREN, &srv, "tv", NULL, 0, 0);
  CHECK(wait_done(req, 10000));
  CHECK(worker_status(req) == PP_ERR_NET);
  CHECK(worker_ran_count() == ran + 2);      /* ran, then retried once */
  CHECK(worker_resume_pending() == 0);       /* consumed by that request */
  worker_release(req);

  req = worker_start(REQ_CHILDREN, &srv, "tv", NULL, 0, 0);
  CHECK(wait_done(req, 10000));
  CHECK(worker_status(req) == PP_ERR_NET);
  CHECK(worker_ran_count() == ran + 3);      /* no retry without a resume */
  worker_release(req);
  CHECK(worker_wait_idle(1000) == 0);
  unsetenv("PP_RESUME_DELAY_MS");
  unsetenv("PP_RESUME_RETRY_MS");
}

/* Where connmanctl is absent (desktop/CI), the post-resume wait on the Wi-Fi
 * reconnect is an immediate no-op: even with PP_RESUME_DELAY_MS unset (the
 * 15 s default) the request runs right away and still retries once. */
static void test_resume_wait_noop_off_device(void) {
  if (pp_wifi_available()) {  /* a host with connman: not a no-op there */
    fprintf(stderr, "  skip: connmanctl present on this host\n");
    return;
  }
  unsetenv("PP_RESUME_DELAY_MS");
  setenv("PP_RESUME_RETRY_MS", "1", 1);
  worker_note_resume();
  int ran = worker_ran_count();
  pp_server srv = { "http://127.0.0.1:1", "REDACTED", "test-client", NULL };
  pp_request *req = worker_start(REQ_CHILDREN, &srv, "tv", NULL, 0, 0);
  CHECK(wait_done(req, 5000));               /* would time out if it waited 15 s */
  CHECK(worker_status(req) == PP_ERR_NET);
  CHECK(worker_ran_count() == ran + 2);      /* ran, then retried once */
  worker_release(req);
  CHECK(worker_wait_idle(1000) == 0);
  unsetenv("PP_RESUME_RETRY_MS");
}

int main(void) {
  pp_init("test-client");
  RUN(test_complete_and_take);
  RUN(test_release_untaken_results);
  RUN(test_cancel_mid_request_no_write_after_free);
  RUN(test_release_stress);
  RUN(test_server_is_copied);
  RUN(test_edge_cases);
  RUN(test_play_requests_fake);
  RUN(test_play_stop_released_in_flight);
  RUN(test_detach_runs_release_cancels);
  RUN(test_unscrobble_fake_and_real_error);
  RUN(test_resume_retry_once);
  RUN(test_resume_wait_noop_off_device);
  pp_cleanup();
  return TEST_RESULT();
}
