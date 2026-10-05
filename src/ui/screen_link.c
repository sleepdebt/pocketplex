/* screen_link.c: PIN-link screen — large code + "go to plex.tv/link" text.
 * Uses plex.h PIN flow (pp_auth_pin_start/poll) via the worker thread.
 * In smoke mode, the worker generates a fake PIN and A-button simulates confirm.
 */
#include "ui.h"
#include "ui/worker.h"
#include "plex/plex.h"
#include "config/config.h"
#include "log.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PIN_POLL_MS  2000
#define PIN_RETRY_MS 2000

typedef struct {
  pp_request *req;     /* owned reference: PIN start or poll in flight */
  char pin[8];
  long pin_id;
  int started;         /* 1 once a PIN code is on screen */
  int linked;
  Uint32 next_at;      /* next poll (or PIN restart) time */
} link_data_t;

static void link_submit(link_data_t *d, pp_req_type type) {
  worker_release(d->req);
  d->req = worker_start(type, NULL, NULL, NULL, d->pin_id, ui_is_smoke_scroll());
}

static void link_save_token(link_data_t *d, const char *token) {
  pp_config cfg;
  pp_config_defaults(&cfg);
  pp_config_load(&cfg, ui_ini_path());
  snprintf(cfg.token, sizeof(cfg.token), "%s", token);
  pp_config_ensure_client_id(&cfg);
  if (pp_config_save(&cfg, ui_ini_path()) != 0) ui_toast("Failed to save token");
  ui_set_auth_token(cfg.token);  /* usable this session even if the save failed */
  memset(&cfg, 0, sizeof(cfg));
  d->linked = 1;
}

/* Collect a finished request and schedule the next step. */
static void link_collect(link_data_t *d) {
  if (!d->req || !worker_is_done(d->req)) return;
  int st = worker_status(d->req);
  Uint32 now = SDL_GetTicks();
  if (!d->started) {                       /* PIN start finished */
    if (st == PP_OK) {
      snprintf(d->pin, sizeof(d->pin), "%s", worker_pin(d->req));
      d->pin_id = worker_pin_id(d->req);
      d->started = 1;
      LOGI("PIN started (smoke=%d)", ui_is_smoke_scroll());
    } else {
      ui_toast(worker_error(d->req)[0] ? worker_error(d->req) : "PIN start failed");
    }
    d->next_at = now + (st == PP_OK ? PIN_POLL_MS : PIN_RETRY_MS);
  } else {                                 /* PIN poll finished */
    if (st == PP_OK && worker_auth_token(d->req)[0]) {
      LOGI("PIN confirmed");
      link_save_token(d, worker_auth_token(d->req));
    } else if (st != 1) {
      ui_toast(worker_error(d->req)[0] ? worker_error(d->req) : "PIN auth failed");
      d->started = 0;                      /* expired or failed: get a new PIN */
    }
    d->next_at = now + PIN_POLL_MS;
  }
  worker_release(d->req);
  d->req = NULL;
}

static void link_render(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  int cx = PP_SCREEN_W / 2;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);

  ui_draw_text("Link to Plex", cx - 70, 60, PP_COLOR_FG);
  ui_draw_text("1. Open plex.tv/link on your phone", PP_MARGIN_L, 110, PP_COLOR_FG);
  ui_draw_text("2. Enter this code:", PP_MARGIN_L, 140, PP_COLOR_FG);

  link_collect(d);

  /* Nothing in flight: start a PIN, or poll the current one (not in smoke). */
  if (!d->req && !d->linked && SDL_TICKS_PASSED(SDL_GetTicks(), d->next_at)) {
    if (!d->started) link_submit(d, REQ_PIN_START);
    else if (!ui_is_smoke_scroll()) link_submit(d, REQ_PIN_POLL);
  }

  if (!d->started) {
    ui_draw_text("Starting...", PP_MARGIN_L, 180, PP_COLOR_DIM);
    ui_draw_text("Menu: quit", PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
    return;
  }

  ui_draw_text(d->pin, cx - 30, 180, PP_COLOR(0xff, 0xff, 0x00));

  if (d->linked) {
    ui_draw_text("Linked!", cx - 40, 240, PP_COLOR(0x00, 0xff, 0x00));
    if (d->linked == 1) {
      d->linked = 2;
      ui_toast("Account linked");
      ui_go(screen_servers_create());
    }
  } else {
    ui_draw_text("Waiting for confirmation...", PP_MARGIN_L, 240, PP_COLOR_DIM);
    ui_draw_spinner(PP_SCREEN_W / 2 - 40, 280, g_spinner_frame);
    g_spinner_frame++;
  }

  ui_draw_text("Menu: quit", PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void link_handle(pp_screen *self, pp_btn btn) {
  link_data_t *d = (link_data_t *)self->data;
  switch (btn) {
  case BTN_A:
    if (ui_is_smoke_scroll() && d->started && !d->linked) {
      LOGI("PIN link confirmed (smoke)");
      d->linked = 1;
    }
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void link_destroy(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  if (d) worker_release(d->req);  /* a still-running worker frees it on exit */
  free(d);
}

pp_screen *screen_link_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  link_data_t *d = (link_data_t *)calloc(1, sizeof(link_data_t));
  if (!d) { free(s); return NULL; }

  s->id = SCREEN_LINK;
  s->data = d;
  s->render = link_render;
  s->handle_button = link_handle;
  s->destroy = link_destroy;

  link_submit(d, REQ_PIN_START);
  return s;
}
