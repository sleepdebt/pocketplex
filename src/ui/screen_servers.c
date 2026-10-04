/* screen_servers.c: server selection, loaded async via worker thread.
 * In production, calls pp_discover_servers with the auth token from config or
 * PIN flow. In smoke mode, the worker dispatches to fake_provider.
 */
#include "ui.h"
#include "ui/worker.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  pp_server *servers;     /* discovered servers (owned by this screen) */
  int count;
  int selected;
  pp_request req;
} servers_data_t;

static void servers_render(pp_screen *self) {
  servers_data_t *d = (servers_data_t *)self->data;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("Servers", PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  /* Check async result */
  if (d->req.type == REQ_SERVERS && !d->req.done) {
    self->loading = 1;
    ui_draw_spinner(PP_SCREEN_W / 2 - 40, PP_SCREEN_H / 2, g_spinner_frame);
    g_spinner_frame++;
    return;
  }

  /* Worker completed */
  if (d->req.type == REQ_SERVERS && d->req.done && !d->servers) {
    if (d->req.status == PP_OK && d->req.servers) {
      d->servers = d->req.servers;
      d->count = d->req.server_count;
    } else {
      /* Error handling: retry on NET/HTTP, relink on AUTH */
      if (d->req.status == PP_ERR_AUTH) {
        ui_toast("Auth expired — relink");
        ui_pop();
        ui_push(screen_link_create());
        d->req.done = 0;
        d->req.type = REQ_NONE;
        return;
      } else if (d->req.status == PP_ERR_NET || d->req.status == PP_ERR_HTTP) {
        if (d->req.error[0]) ui_toast(d->req.error);
        /* Retry */
        d->req.type = REQ_SERVERS;
        d->req.token = ui_get_auth_token();
        d->req.srv = ui_current_server();
        d->req.done = 0;
        worker_submit(&d->req);
        return;
      }
      if (d->req.error[0]) ui_toast(d->req.error);
      d->req.type = REQ_NONE;
    }
  }
  self->loading = 0;

  int y = PP_HEADER_H + 12;
  int i;
  for (i = 0; i < d->count; i++) {
    pp_color col = (i == d->selected) ? PP_COLOR_SEL : PP_COLOR_FG;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", d->servers[i].url);
    ui_draw_text(buf, PP_MARGIN_L, y, col);
    y += PP_LINE_H;
  }
  if (d->count == 0)
    ui_draw_text("No servers found", PP_MARGIN_L, y, PP_COLOR_DIM);

  ui_draw_text("D-pad: select  A: connect  B: back  Menu: quit",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void servers_handle(pp_screen *self, pp_btn btn) {
  servers_data_t *d = (servers_data_t *)self->data;
  if (self->loading) return;
  switch (btn) {
  case BTN_DOWN: if (d->selected < d->count - 1) d->selected++; break;
  case BTN_UP:   if (d->selected > 0) d->selected--; break;
  case BTN_A:
    if (d->count > 0) {
      pp_server *srv = &d->servers[d->selected];
      ui_set_server(srv);
      ui_toast("Connected");
      ui_pop();
      ui_push(screen_home_create());
    }
    break;
  case BTN_SELECT:
    ui_push(screen_settings_create());
    break;
  case BTN_START:
    ui_pop();
    ui_push(screen_home_create());
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
    int i;
    for (i = 0; i < d->count; i++) {
      free(d->servers[i].url);
      free(d->servers[i].token);
      free(d->servers[i].client_id);
    }
    free(d->servers);
    free(d);
  }
}

pp_screen *screen_servers_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  servers_data_t *d = (servers_data_t *)calloc(1, sizeof(servers_data_t));
  if (!d) { free(s); return NULL; }

  d->req.type = REQ_SERVERS;
  d->req.token = ui_get_auth_token();
  d->req.srv = ui_current_server();
  d->req.done = 0;
  s->id = SCREEN_SERVERS;
  s->data = d;
  s->loading = 1;
  s->render = servers_render;
  s->handle_button = servers_handle;
  s->destroy = servers_destroy;
  worker_submit(&d->req);
  return s;
}
