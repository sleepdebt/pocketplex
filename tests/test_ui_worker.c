/* tests/test_ui_worker.c: worker request ownership.
 * The UI may release a request at any time, even while its worker thread is
 * still running; the worker must then free it, never the UI. Run under ASan
 * (see REPORT) to turn any write-after-free into a hard failure; the heap
 * canary check below catches it without ASan too.
 */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test.h"
#include "ui/worker.h"
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
  char *key = strdup("/library/metadata/1/children");
  pp_request *req = worker_start(REQ_CHILDREN, srv, key, NULL, 0, 0);
  pp_servers_free(srv, 1);
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

int main(void) {
  pp_init("test-client");
  RUN(test_complete_and_take);
  RUN(test_release_untaken_results);
  RUN(test_cancel_mid_request_no_write_after_free);
  RUN(test_release_stress);
  RUN(test_server_is_copied);
  RUN(test_edge_cases);
  pp_cleanup();
  return TEST_RESULT();
}
