/* screen_link.c: PIN-link screen — large code + "go to plex.tv/link" text. */
#include "ui.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct {
  char pin[8];
  int poll_ms;
  pp_server *srv;
} link_data_t;

static void link_render(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  (void)d;
  int cx = PP_SCREEN_W / 2;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);

  ui_draw_text("Link to Plex", cx - 70, 60, PP_COLOR_FG);
  ui_draw_text("1. Open plex.tv/link on your phone", PP_MARGIN_L, 110, PP_COLOR_FG);
  ui_draw_text("2. Enter this code:", PP_MARGIN_L, 140, PP_COLOR_FG);
  ui_draw_text(d->pin, cx - 30, 180, PP_COLOR(0xff, 0xff, 0x00));
  ui_draw_text("Waiting for confirmation...", PP_MARGIN_L, 240, PP_COLOR_DIM);

  /* Footer hints */
  ui_draw_text("Menu: quit", PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void link_handle(pp_screen *self, pp_btn btn) {
  link_data_t *d = (link_data_t *)self->data;
  switch (btn) {
  case BTN_A:
    /* Simulate successful PIN link for desktop dev */
    LOGI("PIN link confirmed for server");
    ui_toast("Account linked");
    /* Pop link screen, push servers */
    ui_pop();
    ui_push(screen_servers_create());
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
  (void)d;
}

static void link_destroy(pp_screen *self) {
  link_data_t *d = (link_data_t *)self->data;
  if (d) {
    if (d->srv) { free(d->srv->url); free(d->srv->token); free(d->srv->client_id); free(d->srv); }
    free(d);
  }
}

pp_screen *screen_link_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  link_data_t *d = (link_data_t *)calloc(1, sizeof(link_data_t));
  if (!d) { free(s); return NULL; }
  /* Fake 4-char PIN */
  snprintf(d->pin, sizeof(d->pin), "%04d", (int)(time(NULL) % 10000));
  s->id = SCREEN_LINK;
  s->data = d;
  s->render = link_render;
  s->handle_button = link_handle;
  s->destroy = link_destroy;
  return s;
}
