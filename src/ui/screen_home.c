/* screen_home.c: Home screen — Continue Watching hub + library sections.
 * Loads data via worker thread; shows spinner while pending.
 */
#include "ui.h"
#include "ui/worker.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  pp_list on_deck;     /* continue-watching items */
  pp_list sections;    /* library roots */
  pp_request *req_ondeck;   /* owned references; NULL once collected */
  pp_request *req_sections;
  int ondeck_loaded;   /* 1 after on_deck request completed + processed */
  int sections_loaded; /* 1 after sections request completed + processed */
  int auth_failed;     /* relink already triggered */
  int sel;             /* 0..on_deck.count: -1 means in sections area */
  int in_sections;     /* 0 = on-deck row, 1 = sections row */
} home_data_t;

/* Take a finished request's list (or toast its error) and drop the request. */
static void home_collect(home_data_t *d, pp_request **req, pp_list *out) {
  int st = worker_status(*req);
  if (st == PP_OK) worker_take_list(*req, out);
  else if (st == PP_ERR_AUTH) { if (!d->auth_failed) d->auth_failed = 1; }
  else {
    /* Unreachable saved server: name it and point at the way out. */
    const pp_server *srv = ui_current_server();
    const char *name = srv && srv->name && srv->name[0] ? srv->name : "server";
    char msg[128];
    snprintf(msg, sizeof msg, "Can't reach %s — L1: servers", name);
    ui_toast(msg);
  }
  worker_release(*req);
  *req = NULL;
}

static void home_render(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);

  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("PocketPlex", PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  /* Process completed requests */
  if (!d->ondeck_loaded && worker_is_done(d->req_ondeck)) {
    home_collect(d, &d->req_ondeck, &d->on_deck);
    d->ondeck_loaded = 1;
  }
  if (!d->sections_loaded && worker_is_done(d->req_sections)) {
    home_collect(d, &d->req_sections, &d->sections);
    d->sections_loaded = 1;
  }
  if (d->auth_failed == 1) {
    d->auth_failed = 2;
    ui_toast("Auth expired — relink");
    ui_go(screen_link_create());
    return;
  }

  /* Still loading? */
  if (!d->ondeck_loaded || !d->sections_loaded) {
    self->loading = 1;
    ui_draw_spinner(PP_SCREEN_W / 2 - 40, PP_SCREEN_H / 2, g_spinner_frame);
    g_spinner_frame++;
    return;
  }
  self->loading = 0;

  int total_top = d->on_deck.count;

  /* Continue Watching */
  ui_draw_text("Continue Watching", PP_MARGIN_L, 52, PP_COLOR_FG);
  int y = 80;
  int i;
  for (i = 0; i < total_top && i < 6; i++) {
    pp_item *it = &d->on_deck.items[i];
    pp_color col = (d->in_sections == 0 && d->sel == i) ? PP_COLOR_SEL : PP_COLOR_FG;
    ui_draw_text(it->title, PP_MARGIN_L, y, col);
    if (it->view_offset_ms > 0 && it->duration_ms > 0) {
      int pct = (int)((it->view_offset_ms * 100) / it->duration_ms);
      char buf[64];
      snprintf(buf, sizeof(buf), "Progress: %d%%", pct);
      int w = text_width_px(buf);
      ui_draw_text(buf, PP_SCREEN_W - PP_MARGIN_R - w, y, PP_COLOR_DIM);
    }
    y += PP_LINE_H;
  }

  /* Separator */
  y += 8;
  ui_draw_rect(PP_MARGIN_L, y, PP_SCREEN_W - 2*PP_MARGIN_L, 1, PP_COLOR_DIM);

  /* Sections */
  ui_draw_text("Libraries", PP_MARGIN_L, y + 12, PP_COLOR_FG);
  y += 38;
  for (i = 0; i < d->sections.count; i++) {
    pp_item *it = &d->sections.items[i];
    pp_color col = (d->in_sections == 1 && d->sel - total_top == i)
                   ? PP_COLOR_SEL : PP_COLOR_FG;
    ui_draw_text(it->title, PP_MARGIN_L + 16, y, col);
    y += PP_LINE_H;
  }

  ui_draw_text("D-pad: nav  A: open  Select: settings  L1: servers",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void home_handle(pp_screen *self, pp_btn btn) {
  home_data_t *d = (home_data_t *)self->data;
  if (self->loading) {
    if (btn == BTN_B) ui_pop();
    return;
  }
  int total_top = d->on_deck.count;
  int total_all = total_top + d->sections.count;

  switch (btn) {
  case BTN_DOWN:
    if (d->in_sections == 0) {
      if (d->sel < total_top - 1) d->sel++;
      else { d->in_sections = 1; d->sel = total_top; }
    } else {
      if (d->sel < total_all - 1) d->sel++;
    }
    break;
  case BTN_UP:
    if (d->in_sections == 1 && d->sel == total_top) {
      d->in_sections = 0;
      d->sel = total_top - 1;
    } else if (d->sel > 0) {
      d->sel--;
    }
    break;
  case BTN_A:
    if (!d->in_sections && d->sel >= 0 && d->sel < total_top) {
      pp_item *it = &d->on_deck.items[d->sel];
      ui_go(screen_detail_create(it));
    } else if (d->in_sections && d->sel - total_top >= 0 &&
               d->sel - total_top < d->sections.count) {
      pp_item *it = &d->sections.items[d->sel - total_top];
      ui_go(screen_list_create_key(it->key, it->title));
    }
    break;
  case BTN_SELECT:
    ui_go(screen_settings_create());
    break;
  case BTN_L1:
    ui_go(screen_servers_create());
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void home_destroy(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  if (d) {
    worker_release(d->req_ondeck);   /* running workers free them on exit */
    worker_release(d->req_sections);
    pp_list_free(&d->on_deck);
    pp_list_free(&d->sections);
    free(d);
  }
}

static void home_log_titles(pp_screen *self, int n) {
  home_data_t *d = (home_data_t *)self->data;
  LOGI("walk: Home — On Deck (first %d):", n);
  for (int i = 0; i < n && i < d->on_deck.count; i++)
    LOGI("  [%d] %s", i, d->on_deck.items[i].title);
  LOGI("walk: Home — Sections:");
  for (int i = 0; i < d->sections.count; i++)
    LOGI("  [%d] %s (key=%s)", i, d->sections.items[i].title, d->sections.items[i].key);
}

pp_screen *screen_home_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  home_data_t *d = (home_data_t *)calloc(1, sizeof(home_data_t));
  if (!d) { free(s); return NULL; }
  d->sel = 0;
  d->in_sections = 0;
  s->id = SCREEN_HOME;
  s->data = d;
  s->loading = 1;
  s->render = home_render;
  s->handle_button = home_handle;
  s->destroy = home_destroy;
  s->log_titles = home_log_titles;
  int fake = ui_is_smoke_scroll();
  d->req_ondeck = worker_start(REQ_ON_DECK, ui_current_server(), NULL, NULL, 0, fake);
  d->req_sections = worker_start(REQ_SECTIONS, ui_current_server(), NULL, NULL, 0, fake);
  return s;
}
