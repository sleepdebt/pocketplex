/* screen_link.c: PIN-link screen — large code + "go to plex.tv/link" text.
 * Uses plex.h PIN flow (pp_auth_pin_start/poll) via the worker thread.
 * In smoke mode, the worker generates a fake PIN and A-button simulates confirm.
 */
#include "ui.h"
#include "ui/worker.h"
#include "plex/plex.h"
#include "config/config.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PIN_POLL_MS 2000

typedef struct {
  pp_request req;
  int poll_ms;
  int started;
  int linked;
} link_data_t;

static void link_cancel(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  if (d) worker_cancel(&d->req);
}

static int link_is_busy(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  return d && !worker_is_done(&d->req);
}

static void link_check_pin_start(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  if (!d->started && d->req.type == REQ_PIN_START && worker_is_done(&d->req)) {
    d->started = 1;
    if (d->req.status != PP_OK) {
      snprintf(d->req.pin, sizeof(d->req.pin), "0000");
    }
    d->poll_ms = 0;
    LOGI("PIN started: %s (smoke=%d)", d->req.pin, ui_is_smoke_scroll());
  }
}

static void link_check_pin_poll(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  if (d->req.type == REQ_PIN_POLL && worker_is_done(&d->req)) {
    if (d->req.status == PP_OK && d->req.auth_token[0]) {
      LOGI("PIN confirmed");
      d->linked = 1;
      pp_config cfg;
      pp_config_load(&cfg, ui_ini_path());
      snprintf(cfg.token, sizeof(cfg.token), "%s", d->req.auth_token);
       pp_config_ensure_client_id(&cfg);
      if (pp_config_save(&cfg, ui_ini_path()) != 0)
        ui_toast("Failed to save token");
      else
        ui_set_auth_token(cfg.token);
    } else if (d->req.status == 1) {
    } else {
      ui_toast(d->req.error[0] ? d->req.error : "PIN auth failed");
      d->started = 0;
      d->req.type = REQ_PIN_START;
      worker_submit(&d->req);
    }
  }
}

static void link_render(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  int cx = PP_SCREEN_W / 2;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);

  ui_draw_text("Link to Plex", cx - 70, 60, PP_COLOR_FG);
  ui_draw_text("1. Open plex.tv/link on your phone", PP_MARGIN_L, 110, PP_COLOR_FG);
  ui_draw_text("2. Enter this code:", PP_MARGIN_L, 140, PP_COLOR_FG);

  link_check_pin_start(self);

  if (!d->started) {
    ui_draw_text("Starting...", PP_MARGIN_L, 180, PP_COLOR_DIM);
    ui_draw_text("Menu: quit", PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
    return;
  }

  ui_draw_text(d->req.pin, cx - 30, 180, PP_COLOR(0xff, 0xff, 0x00));

   if (!ui_is_smoke_scroll()) {
     d->poll_ms += 16;
     if (d->poll_ms >= PIN_POLL_MS) {
       d->poll_ms = 0;
       if (!worker_is_done(&d->req)) {
         /* Previous poll still in flight — back off, don't submit. */
         d->poll_ms = PIN_POLL_MS;
       } else {
         d->req.type = REQ_PIN_POLL;
         worker_submit(&d->req);
       }
     }
     link_check_pin_poll(self);
   }

  if (d->linked) {
    ui_draw_text("Linked!", cx - 40, 240, PP_COLOR(0x00, 0xff, 0x00));
    if (d->linked == 1) {
      d->linked = 2;
      ui_toast("Account linked");
      ui_pop();
      ui_push(screen_servers_create());
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
  free(self->data);
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
   s->cancel = link_cancel;
   s->is_busy = link_is_busy;
   s->destroy = link_destroy;

  d->req.type = REQ_PIN_START;
  d->req.done = 0;
  worker_submit(&d->req);

  return s;
}
