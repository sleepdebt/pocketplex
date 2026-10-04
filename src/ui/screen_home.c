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
  pp_request req_ondeck;
  pp_request req_sections;
  int ondeck_loaded;   /* 1 after on_deck request completed + processed */
  int sections_loaded; /* 1 after sections request completed + processed */
  int sel;             /* 0..on_deck.count: -1 means in sections area */
  int in_sections;     /* 0 = on-deck row, 1 = sections row */
} home_data_t;

static void home_render(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);

  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("PocketPlex", PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  /* Process completed requests */
  if (!d->ondeck_loaded && worker_is_done(&d->req_ondeck)) {
    if (d->req_ondeck.status == PP_OK) d->on_deck = d->req_ondeck.result;
    else {
      if (d->req_ondeck.status == PP_ERR_AUTH) {
        ui_toast("Auth expired — relink");
        ui_pop();
        ui_push(screen_link_create());
      } else if (d->req_ondeck.error[0]) ui_toast(d->req_ondeck.error);
    }
    d->ondeck_loaded = 1;
  }
  if (!d->sections_loaded && worker_is_done(&d->req_sections)) {
    if (d->req_sections.status == PP_OK) d->sections = d->req_sections.result;
    else {
      if (d->req_sections.status == PP_ERR_AUTH) {
        ui_toast("Auth expired — relink");
        ui_pop();
        ui_push(screen_link_create());
      } else if (d->req_sections.error[0]) ui_toast(d->req_sections.error);
    }
    d->sections_loaded = 1;
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
  for (i = 0; i < total_top && i < 4; i++) {
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
  if (self->loading) return;
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
      ui_pop();
      ui_push(screen_detail_create(it));
    } else if (d->in_sections && d->sel - total_top >= 0 &&
               d->sel - total_top < d->sections.count) {
      pp_item *it = &d->sections.items[d->sel - total_top];
      ui_pop();
      ui_push(screen_list_create_key(it->key, it->title));
    }
    break;
  case BTN_SELECT:
    ui_push(screen_settings_create());
    break;
  case BTN_L1:
    ui_pop();
    ui_push(screen_servers_create());
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void home_cancel(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  if (d) {
    if (d->req_ondeck.type != REQ_NONE) worker_cancel(&d->req_ondeck);
    if (d->req_sections.type != REQ_NONE) worker_cancel(&d->req_sections);
  }
}

static int home_is_busy(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  return d && (!d->ondeck_loaded || !d->sections_loaded);
}

static void home_destroy(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  if (d) {
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
  /* Start async loads BEFORE setting type/done */
  d->req_ondeck.type = REQ_ON_DECK;
  d->req_ondeck.srv = ui_current_server();
  d->req_ondeck.done = 0;
  d->req_sections.type = REQ_SECTIONS;
  d->req_sections.srv = ui_current_server();
  d->req_sections.done = 0;
  s->id = SCREEN_HOME;
  s->data = d;
  s->loading = 1;
   s->render = home_render;
   s->handle_button = home_handle;
   s->cancel = home_cancel;
   s->is_busy = home_is_busy;
   s->destroy = home_destroy;
  s->log_titles = home_log_titles;
  worker_submit(&d->req_ondeck);
  worker_submit(&d->req_sections);
  return s;
}
