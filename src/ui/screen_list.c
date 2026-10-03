/* screen_list.c: generic paged list with scrollbar, built for 2000-item perf. */
#include "ui.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  pp_list_model model;   /* owned copy of items (shallow) */
  pp_list_view  view;
  char title[128];
} list_data_t;

static void list_render(pp_screen *self) {
  list_data_t *d = (list_data_t *)self->data;
  (void)d;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text(d->title, PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("List  |  Library  |  Settings", PP_SCREEN_W - 190, 10, PP_COLOR_DIM);

  int body_top = PP_HEADER_H + 8;
  int body_h = PP_SCREEN_H - PP_HEADER_H - PP_FOOTER_H - 16;
  int y = body_top;
  int i;
  int visible = d->view.visible_count;
  for (i = d->view.scroll_top; i < d->view.scroll_top + visible && i < d->model.count; i++) {
    pp_item *it = &d->model.items[i];
    pp_color col = (i == d->view.selected) ? PP_COLOR_SEL : PP_COLOR_FG;
    char buf[200];
    /* Indent TV children with season/episode info */
    if (it->kind == PP_EPISODE && it->subtitle[0])
      snprintf(buf, sizeof(buf), "  %s", it->subtitle);
    else
      snprintf(buf, sizeof(buf), "%s", it->title);

    /* Truncate to fit */
    int max_w = PP_SCREEN_W - 2 * PP_MARGIN_L - 24;
    if (text_width_px(buf) > max_w) {
      strncpy(buf, it->title, 110); buf[109] = 0;
    }
    ui_draw_text(buf, PP_MARGIN_L, y, col);
    y += PP_LINE_H;
  }

  /* Scrollbar */
  ui_draw_scrollbar(PP_SCREEN_W - 20, body_top, body_h,
                    d->view.selected, d->model.count);

  /* Footer */
  ui_draw_text("D-pad: move  A: open  B: back  L1/R1: page",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void list_handle(pp_screen *self, pp_btn btn) {
  list_data_t *d = (list_data_t *)self->data;
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
      /* Try children; if none, show detail */
      pp_list children;
      memset(&children, 0, sizeof(children));
      if (fake_children(NULL, it->key, &children) == 0 && children.count > 0) {
        ui_push(screen_list_create(&children, it->title));
      } else {
        ui_pop();
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

static void list_destroy(pp_screen *self) {
  list_data_t *d = (list_data_t *)self->data;
  if (d) {
    /* Fake provider data is static — don't free items */
    d->model.items = NULL; d->model.count = 0;
    free(d);
  }
}

pp_screen *screen_list_create(const pp_list *items, const char *title) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  list_data_t *d = (list_data_t *)calloc(1, sizeof(list_data_t));
  if (!d) { free(s); return NULL; }

  /* Copy items shallowly (the caller's pp_list owns the memory;
   * we just reference the pointers. For the fake provider, the data is static.) */
  d->model.items = items ? items->items : NULL;
  d->model.count = items ? items->count : 0;
  if (title) snprintf(d->title, sizeof(d->title), "%s", title);
  else d->title[0] = 0;

  list_view_init(&d->view, &d->model, PP_VISIBLE);
  s->id = SCREEN_LIST;
  s->data = d;
  s->render = list_render;
  s->handle_button = list_handle;
  s->destroy = list_destroy;
  return s;
}
