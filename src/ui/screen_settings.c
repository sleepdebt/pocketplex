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

static int settings_save(settings_data_t *d) {
  return pp_config_save(&d->cfg, ui_ini_path()) == 0;
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
      ui_toast(settings_save(d) ? d->cfg.quality : "Failed to save settings");
    } else if (d->sel == 1) {
      int burn = (strcmp(d->cfg.subtitles, "burn") == 0);
      snprintf(d->cfg.subtitles, sizeof(d->cfg.subtitles), "%s", burn ? "off" : "burn");
      if (!settings_save(d)) ui_toast("Failed to save settings");
      else ui_toast(d->cfg.subtitles[0] == 'b' ? "Subtitles on" : "Subtitles off");
    } else if (d->sel == 2) {
      memset(d->cfg.token, 0, sizeof(d->cfg.token));
      int saved = settings_save(d);
      ui_sign_out();  /* clears the session and resets the stack to Link */
      ui_toast(saved ? "Signed out" : "Signed out (failed to save)");
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
  settings_data_t *d = (settings_data_t *)self->data;
  if (d) memset(&d->cfg, 0, sizeof(d->cfg));  /* holds the token */
  free(d);
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
  s->destroy = settings_destroy;
  return s;
}
