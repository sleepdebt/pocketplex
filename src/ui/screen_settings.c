/* screen_settings.c: quality preset, subtitles, sign out. */
#include "ui.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>

typedef struct {
  pp_settings *settings;
  int sel;       /* 0=quality, 1=subtitles, 2=sign out */
  int sub_sel;   /* temp value when toggling quality */
} settings_data_t;

static void settings_render(pp_screen *self) {
  settings_data_t *d = (settings_data_t *)self->data;
  (void)d;
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_HEADER_H, PP_COLOR(0x2a, 0x2a, 0x33));
  ui_draw_text("Settings", PP_MARGIN_L, 10, PP_COLOR_FG);
  ui_draw_text("A: toggle  B: back  Menu: quit",
               PP_SCREEN_W - 200, 10, PP_COLOR_DIM);

  int y = PP_HEADER_H + 20;
  const char *labels[] = { "Video Quality", "Subtitles", "Sign Out", NULL };
  int i;
  for (i = 0; labels[i]; i++) {
    pp_color col = (i == d->sel) ? PP_COLOR_SEL : PP_COLOR_FG;
    ui_draw_text(labels[i], PP_MARGIN_L, y, col);
    char val[64];
    if (i == 0)
      snprintf(val, sizeof(val), "%s", d->settings->quality_480p ? "480p" : "360p");
    else if (i == 1)
      snprintf(val, sizeof(val), "%s", d->settings->subtitles ? "burn in" : "off");
    else
      snprintf(val, sizeof(val), "%s", d->settings->signed_in ? "signed in" : "signed out");
    int w = text_width_px(val);
    ui_draw_text(val, PP_SCREEN_W - PP_MARGIN_R - w, y, PP_COLOR_DIM);
    y += PP_LINE_H;
  }

  ui_draw_text("D-pad: navigate  A: toggle  B: back",
               PP_MARGIN_L, PP_SCREEN_H - 24, PP_COLOR_DIM);
}

static void settings_handle(pp_screen *self, pp_btn btn) {
  settings_data_t *d = (settings_data_t *)self->data;
  int count = 3;
  switch (btn) {
  case BTN_DOWN: if (d->sel < count - 1) d->sel++; break;
  case BTN_UP:   if (d->sel > 0) d->sel--; break;
  case BTN_A:
    if (d->sel == 0) d->settings->quality_480p = !d->settings->quality_480p;
    else if (d->sel == 1) d->settings->subtitles = !d->settings->subtitles;
    else if (d->sel == 2) {
      d->settings->signed_in = 0;
      ui_toast("Signed out");
      ui_pop(); /* back to home */
      ui_pop();
    }
    ui_toast(d->sel == 0 ? (d->settings->quality_480p ? "480p" : "360p")
             : d->sel == 1 ? (d->settings->subtitles ? "Subtitles on" : "Subtitles off")
             : "Signing out...");
    break;
  case BTN_B:
    ui_pop();
    break;
  case BTN_MENU:
    ui_pop();
    break;
  default:
    break;
  }
}

static void settings_destroy(pp_screen *self) {
  free(self->data);
}

static int settings_is_busy(pp_screen *self) {
  (void)self;
  return 0;  /* no async work */
}

pp_screen *screen_settings_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  settings_data_t *d = (settings_data_t *)calloc(1, sizeof(settings_data_t));
  if (!d) { free(s); return NULL; }
  d->settings = fake_settings();
  d->sel = 0;
  s->id = SCREEN_SETTINGS;
  s->data = d;
   s->render = settings_render;
   s->handle_button = settings_handle;
   s->is_busy = settings_is_busy;
   s->destroy = settings_destroy;
  return s;
}
