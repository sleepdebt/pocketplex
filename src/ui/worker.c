/* worker.c: async worker thread for provider calls.
 * Runs fake_provider/plex calls on a separate SDL_Thread so the UI
 * never blocks >50 ms. While pending, ui_draw_spinner() animates.
 */
#include "ui/worker.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>

int g_spinner_frame = 0;

/* Global delay from PP_FAKE_DELAY_MS env var (simulates network latency). */
static int fake_delay_ms(void) {
  const char *s = getenv("PP_FAKE_DELAY_MS");
  if (!s) return 0;
  int v = atoi(s);
  return v > 0 ? v : 0;
}

static int worker_thread(void *arg) {
  pp_request *req = (pp_request *)arg;
  /* Simulate network latency */
  int delay = fake_delay_ms();
  if (delay > 0) SDL_Delay(delay);

  switch (req->type) {
  case REQ_SERVERS:
    req->status = fake_servers(&req->servers, &req->server_count);
    break;
  case REQ_SECTIONS:
    req->status = fake_sections(NULL, &req->result);
    break;
  case REQ_CHILDREN:
    req->status = fake_children(NULL, req->key, &req->result);
    break;
  case REQ_ON_DECK:
    req->status = fake_on_deck(NULL, &req->result);
    break;
  default:
    req->status = PP_ERR_ARG;
    break;
  }
  req->done = 1;
  return 0;
}

int worker_submit(pp_request *req) {
  if (!req || req->type == REQ_NONE) return -1;
  req->done = 0;
  req->status = 0;
  SDL_Thread *t = SDL_CreateThread(worker_thread, "pp-worker", req);
  if (!t) {
    req->done = 1;
    req->status = PP_ERR_NOMEM;
    return -1;
  }
  SDL_DetachThread(t);
  return 0;
}

int worker_check(pp_request *req) {
  if (!req) return 0;
  return req->done;
}

void ui_draw_spinner(int x, int y, int frame) {
  const char spinner[] = "|/-\\";
  char buf[8];
  snprintf(buf, sizeof(buf), "Loading %c", spinner[frame % 4]);
  ui_draw_text(buf, x, y, PP_COLOR_FG);
}
