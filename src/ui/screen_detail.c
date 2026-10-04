/* screen_detail.c: item detail — title, year, duration, summary, resume/play. */
#include "ui.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  pp_item item;
} detail_data_t;

static void detail_render(pp_screen *self) {
  detail_data_t *d = (detail_data_t *)self->data;
  pp_item *it = &d->item;
  (void)d;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("Details", PP_MARGIN_L, 10, PP_COLOR_FG);

  int y = PP_HEADER_H + 20;
  /* Title (bold) */
  ui_draw_text(it->title, PP_MARGIN_L, y, PP_COLOR_FG);
  y += PP_LINE_H;

  /* Subtitle / year / duration */
  char meta[256];
  if (it->subtitle[0])
    snprintf(meta, sizeof(meta), "%s · %d", it->subtitle, it->year);
  else
    snprintf(meta, sizeof(meta), "%d", it->year);
  if (it->duration_ms > 0) {
    int mins = it->duration_ms / 60000;
    int n = (int)strlen(meta);
    snprintf(meta + n, sizeof(meta) - n, " \xc2\xb7 %dh%02dm", mins / 60, mins % 60);
  }
  ui_draw_text(meta, PP_MARGIN_L, y, PP_COLOR_DIM);
  y += PP_LINE_H;

  /* Watched badge */
  if (it->watched) {
    ui_draw_text("[watched]", PP_MARGIN_L, y, PP_COLOR(0x44, 0xaa, 0x44));
    y += PP_LINE_H;
  }

  /* Resume point */
  if (it->view_offset_ms > 0 && it->duration_ms > 0) {
    int pct = (int)((it->view_offset_ms * 100) / it->duration_ms);
    snprintf(meta, sizeof(meta), "Resume at %d%% (%d:%02d)",
             pct,
             (int)(it->view_offset_ms / 60000),
             (int)((it->view_offset_ms / 1000) % 60));
    ui_draw_text(meta, PP_MARGIN_L, y, PP_COLOR(0xff, 0xaa, 0x00));
    y += PP_LINE_H;
  }

  /* Summary */
  y += 10;
  ui_draw_text("Summary:", PP_MARGIN_L, y, PP_COLOR_DIM);
  y += PP_LINE_H;
  /* Wrap summary at ~40 chars */
  char wrapped[512];
  strncpy(wrapped, it->summary, sizeof(wrapped) - 1);
  wrapped[sizeof(wrapped)-1] = 0;
  char *line_start = wrapped;
  char *p = wrapped;
  int line_len = 0;
  while (*p) {
    if (*p == ' ' && line_len > 36) {
      *p = 0;
      ui_draw_text(line_start, PP_MARGIN_L, y, PP_COLOR_FG);
      y += PP_LINE_H;
      line_start = p + 1;
      line_len = 0;
    } else {
      line_len++;
    }
    p++;
  }
  if (line_start < p) {
    ui_draw_text(line_start, PP_MARGIN_L, y, PP_COLOR_FG);
    y += PP_LINE_H;
  }

  /* Footer button hints */
  ui_draw_text("A: play  B: back  Y: mark watched",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void detail_handle(pp_screen *self, pp_btn btn) {
  detail_data_t *d = (detail_data_t *)self->data;
  (void)d;
  switch (btn) {
  case BTN_B:
    ui_pop();
    break;
  case BTN_A:
    ui_toast("Playing...");
    LOGI("playing item %s", d->item.rating_key);
    break;
  case BTN_Y:
    ui_toast("Toggled watched");
    d->item.watched = !d->item.watched;
    break;
  case BTN_START:
    ui_pop();
    ui_push(screen_home_create());
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void detail_destroy(pp_screen *self) {
  free(self->data);
}

static int detail_is_busy(pp_screen *self) {
  (void)self;
  return 0;
}

static void detail_log_titles(pp_screen *self, int n) {
  detail_data_t *d = (detail_data_t *)self->data;
  (void)n;
  LOGI("  title: %s", d->item.title);
  LOGI("  subtitle: %s", d->item.subtitle);
  LOGI("  year/duration: %d / %ld ms", d->item.year, d->item.duration_ms);
  LOGI("  resume: %ld ms", d->item.view_offset_ms);
}

pp_screen *screen_detail_create(const pp_item *item) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  detail_data_t *d = (detail_data_t *)calloc(1, sizeof(detail_data_t));
  if (!d) { free(s); return NULL; }
  if (item) d->item = *item;
  s->id = SCREEN_DETAIL;
  s->data = d;
  s->render = detail_render;
  s->handle_button = detail_handle;
   s->destroy = detail_destroy;
   s->is_busy = detail_is_busy;
  s->log_titles = detail_log_titles;
  return s;
}
