/* screen_list.c: generic paged list with scrollbar, built for 2000-item perf.
 * Supports both preloaded data and async key-based loading via worker thread.
 * In production, calls pp_children (via worker) with the real plex.h server.
 * In smoke mode, the worker dispatches to fake_provider.
 */
#include "ui.h"
#include "ui/worker.h"
#include "plex/plex.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  pp_list_model model;
  pp_list_view  view;
  pp_request req;
  char key_buf[256];  /* deep copy of req.key to survive screen pops */
  char title[128];
  int async;
  int error_shown;
} list_data_t;

static int is_container(pp_kind k) {
  return k == PP_SECTION || k == PP_SHOW || k == PP_SEASON ||
         k == PP_ARTIST || k == PP_ALBUM || k == PP_DIR;
}

static void list_render(pp_screen *self) {
  list_data_t *d = (list_data_t *)self->data;

  /* Check for async completion */
  if (d->async && !d->req.done) {
    self->loading = 1;
    ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
    ui_draw_text(d->title, PP_MARGIN_L, 10, PP_COLOR_FG);
    ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);
    ui_draw_spinner(PP_SCREEN_W / 2 - 40, PP_SCREEN_H / 2, g_spinner_frame);
    g_spinner_frame++;
    return;
  } else if (d->async && d->req.done) {
    if (d->req.status == PP_OK) {
      d->model.items = d->req.result.items;
      d->model.count = d->req.result.count;
    } else {
      if (d->req.status == PP_ERR_AUTH) {
        ui_toast("Auth expired — relink");
        ui_pop();
        ui_push(screen_link_create());
        d->async = 0;
        return;
      } else if (!d->error_shown) {
        if (d->req.error[0]) ui_toast(d->req.error);
        d->error_shown = 1;
        /* Retry the request */
        d->req.srv = ui_current_server();
        d->req.done = 0;
        d->req.status = 0;
        worker_submit(&d->req);
        return;
      }
    }
    self->loading = 0;
    d->async = 0;
  }

  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text(d->title, PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  if (d->model.count == 0) {
    ui_draw_text("Empty", PP_MARGIN_L, 80, PP_COLOR_DIM);
  }

  int body_top = PP_HEADER_H + 8;
  int body_h = PP_SCREEN_H - PP_HEADER_H - PP_FOOTER_H - 16;
  int y = body_top;
  int visible = d->view.visible_count;
  int i;
  for (i = d->view.scroll_top;
       i < d->view.scroll_top + visible && i < d->model.count; i++) {
    pp_item *it = &d->model.items[i];
    pp_color col = (i == d->view.selected) ? PP_COLOR_SEL : PP_COLOR_FG;
    char buf[200];
    if (it->kind == PP_EPISODE && it->subtitle[0])
      snprintf(buf, sizeof(buf), "%s", it->subtitle);
    else
      snprintf(buf, sizeof(buf), "%s", it->title);

    int max_w = PP_SCREEN_W - 2 * PP_MARGIN_L - 24;
    if (text_width_px(buf) > max_w) {
      strncpy(buf, it->title, 110);
      buf[109] = 0;
    }
    ui_draw_text(buf, PP_MARGIN_L, y, col);
    y += PP_LINE_H;
  }

  /* Scrollbar */
  if (d->model.count > 0)
    ui_draw_scrollbar(PP_SCREEN_W - 20, body_top, body_h,
                      d->view.selected, d->model.count);

  /* Footer */
  ui_draw_text("D-pad: move  A: open  B: back  L1/R1: page",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
  char stat[64];
  snprintf(stat, sizeof(stat), "%d / %d", d->view.selected + 1, d->model.count);
  int w = text_width_px(stat);
  ui_draw_text(stat, PP_SCREEN_W - PP_MARGIN_R - w, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void list_handle(pp_screen *self, pp_btn btn) {
  list_data_t *d = (list_data_t *)self->data;
  if (self->loading && d->async) return;
  switch (btn) {
  case BTN_DOWN:
  case BTN_UP:
  case BTN_L1:
  case BTN_R1:
    list_view_handle_button(&d->view, btn);
    break;
  case BTN_A:
    if (d->model.count > 0) {
      pp_item *it = &d->model.items[d->view.selected];
      ui_pop();
      if (is_container(it->kind) && it->key[0]) {
        ui_push(screen_list_create_key(it->key, it->title));
      } else {
        ui_push(screen_detail_create(it));
      }
    }
    break;
  case BTN_B:
    ui_pop();
    break;
  case BTN_START:
    ui_pop();
    ui_push(screen_home_create());
    break;
  case BTN_SELECT:
    ui_push(screen_settings_create());
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void list_cancel(pp_screen *self) {
  list_data_t *d = (list_data_t *)self->data;
  if (d) worker_cancel(&d->req);
}

static int list_is_busy(pp_screen *self) {
  list_data_t *d = (list_data_t *)self->data;
  return d && d->async && !d->req.done;
}

static void list_destroy(pp_screen *self) {
  list_data_t *d = (list_data_t *)self->data;
  if (d) {
    if (d->async && d->req.status == PP_OK) pp_list_free(&d->req.result);
    d->model.items = NULL; d->model.count = 0;
    free(d);
  }
}

static void list_log_titles(pp_screen *self, int n) {
  list_data_t *d = (list_data_t *)self->data;
  for (int i = 0; i < n && i < d->model.count; i++) {
    const pp_item *it = &d->model.items[i];
    LOGI("  [%d] %s", i, it->title);
  }
}

pp_screen *screen_list_create(const pp_list *items, const char *title) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  list_data_t *d = (list_data_t *)calloc(1, sizeof(list_data_t));
  if (!d) { free(s); return NULL; }
  d->model.items = items ? items->items : NULL;
  d->model.count = items ? items->count : 0;
  if (title) snprintf(d->title, sizeof(d->title), "%s", title);
  list_view_init(&d->view, &d->model, PP_VISIBLE);
  s->id = SCREEN_LIST;
  s->data = d;
   s->render = list_render;
   s->handle_button = list_handle;
   s->cancel = list_cancel;
   s->is_busy = list_is_busy;
   s->destroy = list_destroy;
   s->log_titles = list_log_titles;
   return s;
}

pp_screen *screen_list_create_key(const char *key, const char *title) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  list_data_t *d = (list_data_t *)calloc(1, sizeof(list_data_t));
  if (!d) { free(s); return NULL; }
  if (title) snprintf(d->title, sizeof(d->title), "%s", title);
  d->async = 1;
  d->req.type = REQ_CHILDREN;
  if (key) {
    snprintf(d->key_buf, sizeof(d->key_buf), "%s", key);
    d->req.key = d->key_buf;
  }
  d->req.srv = ui_current_server();
  d->req.done = 0;
  list_view_init(&d->view, &d->model, PP_VISIBLE);
  s->id = SCREEN_LIST;
  s->data = d;
  s->loading = 1;
   s->render = list_render;
   s->handle_button = list_handle;
   s->cancel = list_cancel;
   s->is_busy = list_is_busy;
   s->destroy = list_destroy;
   s->log_titles = list_log_titles;
   worker_submit(&d->req);
   return s;
}
