/* screen_servers.c: server selection, loaded async via worker thread.
 * In production, calls pp_discover_servers with the auth token from config or
 * PIN flow. In smoke mode, the worker dispatches to fake_provider.
 */
#include "ui.h"
#include "ui/worker.h"
#include "ui/session.h"
#include "config/config.h"
#include "log.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  pp_server *servers;     /* discovered servers (owned by this screen) */
  int count;
  int selected;
  pp_request *req;        /* owned reference; NULL when idle */
  Uint32 retry_at;        /* next retry time (ms) for backoff */
  int retry_count;        /* consecutive retries for exponential backoff */
  int retry_pending;      /* 1 while waiting for retry_at */
} servers_data_t;

static void servers_submit(servers_data_t *d) {
  const char *tok = ui_get_auth_token();
  LOGI("discovery: start (token %s, attempt %d)", tok ? "set" : "missing", d->retry_count + 1);
  d->req = worker_start(REQ_SERVERS, NULL, NULL, tok, 0, ui_is_smoke_scroll());
}

/* Remember the chosen server so the next launch goes straight to Home. */
static void servers_persist(const pp_server *srv) {
  if (ui_is_smoke_scroll()) return;  /* fake servers never touch the ini */
  pp_config cfg;
  pp_config_defaults(&cfg);
  pp_config_load(&cfg, ui_ini_path());
  int ok = session_apply_server(&cfg, srv) == 0 && pp_config_save(&cfg, ui_ini_path()) == 0;
  memset(&cfg, 0, sizeof cfg);
  if (ok) LOGI("servers: saved choice (server token %s)", srv->token && srv->token[0] ? "set" : "kept");
  else { LOGW("servers: could not save the chosen server"); ui_toast("Could not save server"); }
}

static void servers_render(pp_screen *self) {
  servers_data_t *d = (servers_data_t *)self->data;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("Servers", PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  /* Check async result */
  if (d->req && !worker_is_done(d->req)) {
    self->loading = 1;
    ui_draw_spinner(PP_SCREEN_W / 2 - 40, PP_SCREEN_H / 2, g_spinner_frame);
    g_spinner_frame++;
    return;
  }

  /* Worker completed */
  if (d->req) {
    int st = worker_status(d->req);
    if (st == PP_OK) {
      worker_take_servers(d->req, &d->servers, &d->count);
      d->retry_count = 0;
      LOGI("discovery: %d server(s)", d->count);
    } else {
      LOGW("discovery: failed (%d)", st);
    }
    if (st != PP_OK && worker_error(d->req)[0]) {
      ui_toast(worker_error(d->req));
    }
    worker_release(d->req);
    d->req = NULL;
    if (st == PP_ERR_AUTH) {
      ui_replace(screen_link_create());
      return;
    }
    if (st == PP_ERR_NET || st == PP_ERR_HTTP) {
      /* Exponential backoff: 1, 2, 4, 8, 16 s. */
      int backoff = 1000 * (1 << (d->retry_count < 4 ? d->retry_count : 4));
      d->retry_count++;
      d->retry_at = SDL_GetTicks() + (Uint32)backoff;
      d->retry_pending = 1;
    }
  }

  if (d->retry_pending) {
    self->loading = 1;
    if (SDL_TICKS_PASSED(SDL_GetTicks(), d->retry_at)) {
      d->retry_pending = 0;
      servers_submit(d);
    }
    ui_draw_text("Retrying...  B: back", PP_MARGIN_L, PP_SCREEN_H / 2, PP_COLOR_DIM);
    return;
  }
  self->loading = 0;

  int y = PP_HEADER_H + 12;
  int i;
  for (i = 0; i < d->count; i++) {
    pp_color col = (i == d->selected) ? PP_COLOR_SEL : PP_COLOR_FG;
    char buf[128];
    session_server_label(&d->servers[i], buf, sizeof(buf));
    ui_draw_text(buf, PP_MARGIN_L, y, col);
    y += PP_LINE_H;
  }
  if (d->count == 0)
    ui_draw_text("No servers found  (A: retry)", PP_MARGIN_L, y, PP_COLOR_DIM);

  ui_draw_text("D-pad: select  A: connect  B: back  Menu: quit",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void servers_handle(pp_screen *self, pp_btn btn) {
  servers_data_t *d = (servers_data_t *)self->data;
  if (self->loading) {
    if (btn == BTN_B) ui_pop();
    return;
  }
  switch (btn) {
  case BTN_DOWN: if (d->selected < d->count - 1) d->selected++; break;
  case BTN_UP:   if (d->selected > 0) d->selected--; break;
  case BTN_A:
    if (d->count == 0 && !d->req) {  /* "No servers found": A retries */
      servers_submit(d);
      self->loading = 1;
    } else if (d->count > 0) {
      pp_server *srv = &d->servers[d->selected];
      LOGI("servers: chose #%d of %d", d->selected + 1, d->count);
      ui_set_server(srv);
      servers_persist(srv);
      ui_toast("Connected");
      ui_replace(screen_home_create());
    }
    break;
  case BTN_SELECT:
    ui_push(screen_settings_create());
    break;
  case BTN_START:
    ui_replace(screen_home_create());
    break;
  case BTN_B:
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void servers_destroy(pp_screen *self) {
  servers_data_t *d = (servers_data_t *)self->data;
  if (d) {
    worker_release(d->req);  /* a still-running worker frees it on exit */
    pp_servers_free(d->servers, d->count);
    free(d);
  }
}

pp_screen *screen_servers_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  servers_data_t *d = (servers_data_t *)calloc(1, sizeof(servers_data_t));
  if (!d) { free(s); return NULL; }

  s->id = SCREEN_SERVERS;
  s->data = d;
  s->loading = 1;
  s->render = servers_render;
  s->handle_button = servers_handle;
  s->destroy = servers_destroy;
  servers_submit(d);
  return s;
}
