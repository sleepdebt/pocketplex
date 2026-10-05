/* screen_detail.c: item detail — title, year, duration, summary, resume/play. */
#include "ui.h"
#include "ui/play_state.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  pp_item item;
  int choosing;   /* 1 while the resume / play-from-start choice is open */
  int choice;     /* 0 = resume, 1 = from start */
} detail_data_t;

static int is_playable(pp_kind k) {
  return k == PP_EPISODE || k == PP_MOVIE;
}

static void detail_play(detail_data_t *d, long start_ms) {
  d->choosing = 0;
  pp_screen *p = screen_player_create(&d->item, start_ms, &d->item);
  if (p) ui_go(p);
  else ui_toast("Out of memory");
}

static void draw_choice(detail_data_t *d) {
  char resume[64], t[16];
  play_fmt_time(d->item.view_offset_ms, t, sizeof t);
  snprintf(resume, sizeof resume, "Resume from %s", t);
  const char *opts[2] = { resume, "Play from start" };
  int x = 120, y = 170, w = PP_SCREEN_W - 240, h = 2 * PP_LINE_H + 24;
  ui_fill_rect(x, y, w, h, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_rect(x, y, w, h, PP_COLOR_FG);
  for (int i = 0; i < 2; i++)
    ui_draw_text(opts[i], x + 16, y + 12 + i * PP_LINE_H,
                 i == d->choice ? PP_COLOR_SEL : PP_COLOR_FG);
}

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

  if (d->choosing) draw_choice(d);

  /* Footer button hints */
  ui_draw_text(d->choosing ? "D-pad: choose  A: play  B: cancel"
                           : "A: play  B: back  Y: mark watched",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void detail_handle(pp_screen *self, pp_btn btn) {
  detail_data_t *d = (detail_data_t *)self->data;
  if (d->choosing) {
    switch (btn) {
    case BTN_UP:   d->choice = 0; break;
    case BTN_DOWN: d->choice = 1; break;
    case BTN_A:    detail_play(d, d->choice == 0 ? d->item.view_offset_ms : 0); break;
    case BTN_B:    d->choosing = 0; break;
    default: break;
    }
    return;
  }
  switch (btn) {
  case BTN_B:
    ui_pop();
    break;
  case BTN_A:
    if (!is_playable(d->item.kind)) {
      ui_toast("Not playable");
    } else if (d->item.view_offset_ms > 0) {
      d->choosing = 1;
      d->choice = 0;
    } else {
      detail_play(d, 0);
    }
    break;
  case BTN_Y:
    ui_toast("Toggled watched");
    d->item.watched = !d->item.watched;
    break;
  case BTN_START:
    ui_go(screen_home_create());
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
  s->log_titles = detail_log_titles;
  return s;
}
