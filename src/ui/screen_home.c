/* screen_home.c: Home screen — Continue Watching hub + library sections. */
#include "ui.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  pp_list on_deck;     /* continue-watching items */
  pp_list sections;    /* library roots */
  int sel;             /* 0..on_deck.count: -1 means in sections area */
  int in_sections;     /* 0 = on-deck row, 1 = sections row */
} home_data_t;

static void home_render(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  (void)d;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);

  /* Header */
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("PocketPlex", PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("Home  |  Library  |  Settings", PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  /* Continue Watching */
  ui_draw_text("Continue Watching", PP_MARGIN_L, 52, PP_COLOR_FG);
  int y = 80;
  int i;
  for (i = 0; i < d->on_deck.count && i < 4; i++) {
    pp_item *it = &d->on_deck.items[i];
    pp_color col = (d->in_sections == 0 && d->sel == i) ? PP_COLOR_SEL : PP_COLOR_FG;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", it->title);
    ui_draw_text(buf, PP_MARGIN_L, y, col);
    /* Progress indicator */
    if (it->view_offset_ms > 0 && it->duration_ms > 0) {
      int pct = (int)((it->view_offset_ms * 100) / it->duration_ms);
      snprintf(buf, sizeof(buf), "Progress: %d%%", pct);
      int w = text_width_px(buf);
      ui_draw_text(buf, PP_SCREEN_W - PP_MARGIN_R - w, y, PP_COLOR_DIM);
    }
    y += PP_LINE_H;
  }

  /* Separator */
  y += 8;
  ui_draw_rect(PP_MARGIN_L, y, PP_SCREEN_W - 2*PP_MARGIN_L, 1, PP_COLOR_DIM);

  /* Sections / Libraries */
  ui_draw_text("Libraries", PP_MARGIN_L, y + 12, PP_COLOR_FG);
  y += 38;
  for (i = 0; i < d->sections.count; i++) {
    pp_item *it = &d->sections.items[i];
    pp_color col = (d->in_sections == 1 && d->sel - d->on_deck.count == i)
                   ? PP_COLOR_SEL : PP_COLOR_FG;
    ui_draw_text(it->title, PP_MARGIN_L + 16, y, col);
    y += PP_LINE_H;
  }

  /* Footer */
  ui_draw_text("D-pad: navigate  A: open  B: back  Select: settings",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void home_handle(pp_screen *self, pp_btn btn) {
  home_data_t *d = (home_data_t *)self->data;
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
      /* Sections don't have a list model, so load children */
      pp_list children;
      memset(&children, 0, sizeof(children));
      if (fake_children(NULL, it->key, &children) == 0) {
        ui_push(screen_list_create(&children, it->title));
      }
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

static void home_destroy(pp_screen *self) {
  home_data_t *d = (home_data_t *)self->data;
  if (d) {
    /* Fake provider data is static — don't free items */
    d->on_deck.items = NULL; d->on_deck.count = 0;
    d->sections.items = NULL; d->sections.count = 0;
    free(d);
  }
}

pp_screen *screen_home_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  home_data_t *d = (home_data_t *)calloc(1, sizeof(home_data_t));
  if (!d) { free(s); return NULL; }
  fake_on_deck(NULL, &d->on_deck);
  fake_sections(NULL, &d->sections);
  d->sel = 0;
  d->in_sections = 0;
  s->id = SCREEN_HOME;
  s->data = d;
  s->render = home_render;
  s->handle_button = home_handle;
  s->destroy = home_destroy;
  return s;
}
