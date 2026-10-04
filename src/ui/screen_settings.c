/* screen_settings.c: quality preset, subtitles, sign out. */
#include "ui.h"
#include "config/config.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  pp_config cfg;
  int sel;
} settings_data_t;

static void settings_render(pp_screen *self) {
  settings_data_t *d = (settings_data_t *)self->data;
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
    if (i == 0) {
      int is_480 = (strcmp(d->cfg.quality, "480p") == 0);
      snprintf(val, sizeof(val), "%s", is_480 ? "480p" : "360p");
    } else if (i == 1) {
      int burn = (strcmp(d->cfg.subtitles, "burn") == 0);
      snprintf(val, sizeof(val), "%s", burn ? "burn in" : "off");
    } else {
      snprintf(val, sizeof(val), "%s", d->cfg.token[0] ? "signed in" : "signed out");
    }
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
    if (d->sel == 0) {
      int is_480 = (strcmp(d->cfg.quality, "480p") == 0);
      snprintf(d->cfg.quality, sizeof(d->cfg.quality), "%s", is_480 ? "360p" : "480p");
      pp_config_save(&d->cfg, ui_ini_path());
      ui_toast(d->cfg.quality);
    } else if (d->sel == 1) {
      int burn = (strcmp(d->cfg.subtitles, "burn") == 0);
      snprintf(d->cfg.subtitles, sizeof(d->cfg.subtitles), "%s", burn ? "off" : "burn");
      pp_config_save(&d->cfg, ui_ini_path());
      ui_toast(d->cfg.subtitles[0] == 'b' ? "Subtitles on" : "Subtitles off");
    } else if (d->sel == 2) {
      d->cfg.token[0] = '\0';
      pp_config_save(&d->cfg, ui_ini_path());
      ui_set_auth_token(NULL);
      ui_toast("Signed out");
      ui_pop();
    }
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
  return 0;
}

pp_screen *screen_settings_create(void) {
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  settings_data_t *d = (settings_data_t *)calloc(1, sizeof(settings_data_t));
  if (!d) { free(s); return NULL; }
  pp_config_defaults(&d->cfg);
  pp_config_load(&d->cfg, ui_ini_path());
  d->sel = 0;
  s->id = SCREEN_SETTINGS;
  s->data = d;
  s->render = settings_render;
  s->handle_button = settings_handle;
  s->is_busy = settings_is_busy;
  s->destroy = settings_destroy;
  return s;
}
