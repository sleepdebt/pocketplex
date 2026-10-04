/* worker.c: async worker thread for provider calls.
 * Runs plex.h / fake_provider calls on a separate SDL_Thread so the UI
 * never blocks >50 ms. While a request is pending, ui_draw_spinner() animates.
 */
#include "ui/worker.h"
#include "ui/fake_provider.h"
#include "ui/ui.h"
#include "log.h"

#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int g_spinner_frame = 0;

/* Global delay from PP_FAKE_DELAY_MS env var (simulates network latency for tests). */
static int fake_delay_ms(void) {
  const char *s = getenv("PP_FAKE_DELAY_MS");
  if (!s) return 0;
  int v = atoi(s);
  return v > 0 ? v : 0;
}

static void set_error(pp_request *req, const char *msg) {
  snprintf(req->error, sizeof(req->error), "%s", msg);
}

static int worker_thread(void *arg) {
  pp_request *req = (pp_request *)arg;
  int delay = fake_delay_ms();
  if (delay > 0) SDL_Delay(delay);

  /* Smoke-scroll mode uses the fake provider; production uses plex.h. */
  if (req->use_fake) {
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
    case REQ_PIN_START:
      snprintf(req->pin, sizeof(req->pin), "%04d", (int)(time(NULL) % 10000));
      req->pin_id = 12345;
      req->status = PP_OK;
      break;
    case REQ_PIN_POLL:
      req->status = 1; /* pending in smoke mode */
      break;
    default:
      req->status = PP_ERR_ARG;
      set_error(req, "unsupported request type");
      break;
    }
  } else {
    switch (req->type) {
    case REQ_SERVERS:
      req->status = pp_discover_servers(req->token, &req->servers, &req->server_count);
      break;
    case REQ_SECTIONS:
      req->status = pp_sections(req->srv, &req->result);
      break;
    case REQ_CHILDREN:
      req->status = pp_children(req->srv, req->key, &req->result);
      break;
    case REQ_ON_DECK:
      req->status = pp_on_deck(req->srv, &req->result);
      break;
    case REQ_PIN_START:
      req->status = pp_auth_pin_start(req->pin, &req->pin_id);
      break;
    case REQ_PIN_POLL:
      req->status = pp_auth_pin_poll(req->pin_id, req->auth_token, sizeof(req->auth_token));
      break;
    default:
      req->status = PP_ERR_ARG;
      set_error(req, "unsupported request type");
      break;
    }
  }

  if (req->status != PP_OK) {
    switch (req->status) {
    case PP_ERR_AUTH:  set_error(req, "Authentication failed (relink)"); break;
    case PP_ERR_NET:   set_error(req, "Network error (retry)"); break;
    case PP_ERR_HTTP:  set_error(req, "Server error (retry)"); break;
    case PP_ERR_PARSE: set_error(req, "Parse error"); break;
    case PP_ERR_NOMEM: set_error(req, "Out of memory"); break;
    default:           set_error(req, "Request failed"); break;
    }
  }
  req->done = 1;
  return 0;
}

int worker_submit(pp_request *req) {
  if (!req || req->type == REQ_NONE) return -1;
  req->done = 0;
  req->status = 0;
  req->error[0] = '\0';
  req->use_fake = ui_is_smoke_scroll() ? 1 : 0;
  SDL_Thread *t = SDL_CreateThread(worker_thread, "pp-worker", req);
  if (!t) {
    req->done = 1;
    req->status = PP_ERR_NOMEM;
    set_error(req, "Thread creation failed");
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
  char buf[16];
  snprintf(buf, sizeof(buf), "Loading %c", spinner[frame % 4]);
  ui_draw_text(buf, x, y, PP_COLOR_FG);
}
