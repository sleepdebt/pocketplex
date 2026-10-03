/* ui.c: screen stack, font/text rendering, main event+render loop. */
#include "ui.h"
#include "ui/ui_platform.h"
#include "ui/fake_provider.h"
#include "log.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SCREENS 16
#define MAX_TOAST_LEN 128

static SDL_Renderer *g_ren = NULL;
static TTF_Font *g_font = NULL;
static TTF_Font *g_font_bold = NULL;
static int g_font_pt = 20;

static pp_screen *g_stack[MAX_SCREENS];
static int g_stack_top = -1;
static int g_stack_count = 0;

static char g_toast_msg[MAX_TOAST_LEN] = {0};
static int g_toast_ms_left = 0;
static const int TOAST_LIFETIME_MS = 3000;

static pp_server *g_servers = NULL;
static int g_server_count = 0;
static pp_server *g_current_server = NULL;

/* ---- Text texture cache --------------------------------------------------- */

typedef struct tex_cache {
  char key[256];
  SDL_Texture *tex;
  int w, h;
  struct tex_cache *next;
} tex_cache;

static tex_cache *g_cache = NULL;

static void cache_init(void) {
  g_cache = NULL;
}

static SDL_Texture *get_text_texture(const char *str, pp_color color) {
  tex_cache *c;
  for (c = g_cache; c; c = c->next) {
    if (strcmp(c->key, str) == 0) return c->tex;
  }
  SDL_Color sdl_col = { color.r, color.g, color.b, color.a };
  SDL_Surface *surf = TTF_RenderText_Blended(g_font, str, sdl_col);
  if (!surf) return NULL;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
  SDL_FreeSurface(surf);
  if (!tex) return NULL;

  c = (tex_cache *)malloc(sizeof(tex_cache));
  if (!c) { SDL_DestroyTexture(tex); return NULL; }
  c->tex = tex;
  SDL_QueryTexture(tex, NULL, NULL, &c->w, &c->h);
  snprintf(c->key, sizeof(c->key), "%s", str);
  c->next = g_cache;
  g_cache = c;
  return tex;
}

static void cache_clear(void) {
  tex_cache *c = g_cache;
  while (c) {
    tex_cache *next = c->next;
    SDL_DestroyTexture(c->tex);
    free(c);
    c = next;
  }
  g_cache = NULL;
}

/* ---- Rendering primitives ------------------------------------------------- */

int ui_draw_text(const char *str, int x, int y, pp_color color) {
  if (!g_ren || !g_font) return -1;
  SDL_Texture *tex = get_text_texture(str, color);
  if (!tex) return -1;
  int w = 0, h = 0;
  tex_cache *c;
  for (c = g_cache; c; c = c->next)
    if (strcmp(c->key, str) == 0) { w = c->w; h = c->h; break; }
  SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
  SDL_Rect dst = { x, y, w, h };
  SDL_RenderCopy(g_ren, tex, NULL, &dst);
  return 0;
}

void ui_fill_rect(int x, int y, int w, int h, pp_color color) {
  SDL_SetRenderDrawColor(g_ren, color.r, color.g, color.b, color.a);
  SDL_Rect r = { x, y, w, h };
  SDL_RenderFillRect(g_ren, &r);
}

void ui_draw_rect(int x, int y, int w, int h, pp_color color) {
  SDL_SetRenderDrawColor(g_ren, color.r, color.g, color.b, color.a);
  SDL_Rect r = { x, y, w, h };
  SDL_RenderDrawRect(g_ren, &r);
}

void ui_draw_scrollbar(int x, int y, int h_bar, int selected, int total) {
  if (total <= 0) return;
  int thumb_h = (h_bar * h_bar) / (h_bar + total);
  if (thumb_h < 8) thumb_h = 8;
  int max_scroll = total - 1;
  int thumb_y = selected * (h_bar - thumb_h) / (max_scroll > 0 ? max_scroll : 1);
  ui_fill_rect(x, y, 8, h_bar, PP_COLOR_DIM);
  ui_fill_rect(x, y + thumb_y, 8, thumb_h, PP_COLOR_SEL);
  ui_draw_rect(x, y, 8, h_bar, PP_COLOR_FG);
}

int text_width_px(const char *str) {
  if (!g_font || !str) return 0;
  int w = 0, h = 0;
  TTF_SizeText(g_font, str, &w, &h);
  return w;
}

/* ---- Screen stack -------------------------------------------------------- */

void ui_push(pp_screen *s) {
  if (g_stack_count >= MAX_SCREENS) return;
  g_stack[++g_stack_top] = s;
  g_stack_count = g_stack_top + 1;
}

void ui_pop(void) {
  if (g_stack_top < 0) return;
  pp_screen *s = g_stack[g_stack_top];
  if (s && s->destroy) s->destroy(s);
  g_stack[g_stack_top] = NULL;
  g_stack_top--;
  if (g_stack_top < 0) g_stack_top = -1;
}

static pp_screen *current_screen(void) {
  return (g_stack_top >= 0) ? g_stack[g_stack_top] : NULL;
}

void ui_toast(const char *msg) {
  if (!msg) return;
  snprintf(g_toast_msg, sizeof(g_toast_msg), "%s", msg);
  g_toast_ms_left = TOAST_LIFETIME_MS;
}

/* ---- Font init ----------------------------------------------------------- */

static int font_load(void) {
  const char *font_paths[] = {
    "assets/font/DejaVuSansMono.ttf",
    "../assets/font/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    NULL
  };
  int i;
  for (i = 0; font_paths[i]; i++) {
    g_font = TTF_OpenFont(font_paths[i], g_font_pt);
    if (g_font) break;
  }
  if (!g_font) {
    LOGE("TTF_OpenFont failed: %s", TTF_GetError());
    return -1;
  }
  for (i = 0; font_paths[i]; i++) {
    g_font_bold = TTF_OpenFont(font_paths[i], g_font_pt + 2);
    if (g_font_bold) break;
  }
  if (!g_font_bold) g_font_bold = g_font;
  return 0;
}

/* ---- Main loop ----------------------------------------------------------- */

int ui_init(void) {
  g_ren = plat_renderer();
  if (!g_ren) { LOGE("no renderer"); return -1; }
  if (font_load() != 0) return -1;
  cache_init();

  /* Discover fake servers for the home/servers flow. */
  if (fake_servers(&g_servers, &g_server_count) == 0 && g_server_count > 0)
    g_current_server = &g_servers[0];

  return 0;
}

void ui_quit(void) {
  while (g_stack_top >= 0) ui_pop();
  cache_clear();
  if (g_font_bold && g_font_bold != g_font) TTF_CloseFont(g_font_bold);
  if (g_font) TTF_CloseFont(g_font);
  g_font = g_font_bold = NULL;
  if (g_servers) {
    int i;
    for (i = 0; i < g_server_count; i++) {
      free(g_servers[i].url);
      free(g_servers[i].token);
      free(g_servers[i].client_id);
    }
    free(g_servers);
    g_servers = NULL;
    g_server_count = 0;
    g_current_server = NULL;
  }
}

pp_server *ui_current_server(void) {
  return g_current_server;
}

void ui_render_toast(void) {
  if (g_toast_ms_left <= 0) return;
  ui_fill_rect(0, PP_SCREEN_H - 48, PP_SCREEN_W, 48, PP_COLOR(0, 0, 0));
  ui_draw_rect(0, PP_SCREEN_H - 48, PP_SCREEN_W, 48, PP_COLOR_FG);
  ui_draw_text(g_toast_msg, PP_MARGIN_L, PP_SCREEN_H - 36, PP_COLOR_FG);
}

void ui_run(void) {
  int running = 1;
  Uint32 start_tick = SDL_GetTicks();
  long exit_after = ui_get_exit_after_ms();
  Uint32 last_tick = start_tick;
  Uint32 fps_check = start_tick;
  int frame_count = 0;

  /* Start on the Link screen if not authenticated, else Home. */
  if (g_current_server && g_current_server->token) {
    ui_push(screen_home_create());
  } else {
    ui_push(screen_link_create());
  }

  while (running && g_stack_top >= 0) {
    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - last_tick;
    last_tick = now;
    if (g_toast_ms_left > 0) {
      g_toast_ms_left -= (int)elapsed;
      if (g_toast_ms_left < 0) g_toast_ms_left = 0;
    }

    if (exit_after > 0 && (long)(now - start_tick) >= exit_after) break;

    /* FPS logging every 1 s */
    frame_count++;
    if (now - fps_check >= 1000) {
      LOGI("fps: %d (stack=%d)", frame_count, g_stack_count);
      frame_count = 0;
      fps_check = now;
    }

    pp_screen *s = current_screen();
    if (!s) break;

    SDL_SetRenderDrawColor(g_ren, 0x18, 0x18, 0x1c, 0xff);
    SDL_RenderClear(g_ren);

    if (s->render) s->render(s);
    ui_render_toast();

    plat_present();

    pp_btn btn = plat_poll_button();
    if (btn == BTN_MENU) { running = 0; break; }
    if (btn != BTN_NONE && s->handle_button) s->handle_button(s, btn);
  }
}
