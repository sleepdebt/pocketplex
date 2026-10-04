/* ui.c: screen stack, font/text rendering, main event+render loop. */
#include "ui.h"
#include "ui/ui_platform.h"
#include "config/config.h"
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

TTF_Font *ui_font(void) { return g_font; }
TTF_Font *ui_font_bold(void) { return g_font_bold ? g_font_bold : g_font; }

static pp_server *g_current_server = NULL;
static int g_server_owned = 0;  /* 1 if g_current_server was deep-copied */
static char g_auth_token[256] = {0};
static char g_ini_path[512] = {0};

/* ---- Smoke scroll + max frame tracking ------------------------------------ */
static int g_smoke_scroll = 0;
static int g_smoke_walk = 0;
static int g_smoke_phase = 0;
static Uint32 g_smoke_start = 0;
static int g_smoke_scroll_count = 0;
static Uint32 g_max_frame_ms = 0;

static pp_screen *g_stack[MAX_SCREENS];
static int g_stack_top = -1;
static int g_stack_count = 0;

/* Deferred pop queue: screens are freed at end-of-frame to avoid
 * use-after-free with in-flight worker threads. */
static pp_screen *g_pop_queue[MAX_SCREENS];
static int g_pop_count = 0;

static char g_toast_msg[MAX_TOAST_LEN] = {0};
static int g_toast_ms_left = 0;
static const int TOAST_LIFETIME_MS = 3000;

/* ---- Text texture cache (bounded LRU, keyed by text+colour) ---------------- */

#define CACHE_MAX 64

typedef struct tex_cache {
  char key[256];          /* text + colour hash */
  SDL_Texture *tex;
  int w, h;
  struct tex_cache *next;
} tex_cache;

static tex_cache *g_cache_head = NULL;
static int g_cache_count = 0;

static void cache_init(void) {
  g_cache_head = NULL;
  g_cache_count = 0;
}

static void cache_move_to_front(tex_cache *c) {
  if (g_cache_head == c) return;
  tex_cache *p = g_cache_head;
  while (p && p->next != c) p = p->next;
  if (p) p->next = c->next;
  c->next = g_cache_head;
  g_cache_head = c;
}

static void cache_evict_lru(void) {
  if (g_cache_count < CACHE_MAX) return;
  tex_cache *p = g_cache_head;
  while (p && p->next && p->next->next) p = p->next;  /* find second-to-last */
  if (!p) return;
  tex_cache *lru = p->next;
  if (lru) {
    p->next = NULL;
    SDL_DestroyTexture(lru->tex);
    free(lru);
    g_cache_count--;
  }
}

static SDL_Texture *get_text_texture(const char *str, pp_color color, int *w, int *h) {
  char keybuf[260];
  snprintf(keybuf, sizeof(keybuf), "%c%c%c%c:%s", color.r, color.g, color.b, color.a, str);

  tex_cache *c;
  for (c = g_cache_head; c; c = c->next) {
    if (c->key[0] == keybuf[0] && c->key[1] == keybuf[1] &&
        c->key[2] == keybuf[2] && c->key[3] == keybuf[3] &&
        strcmp(c->key + 5, keybuf + 5) == 0) {
      cache_move_to_front(c);
      if (w) *w = c->w;
      if (h) *h = c->h;
      return c->tex;
    }
  }

  SDL_Color sdl_col = { color.r, color.g, color.b, color.a };
  SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font, str, sdl_col);
  if (!surf) return NULL;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
  SDL_FreeSurface(surf);
  if (!tex) return NULL;

  cache_evict_lru();
  c = (tex_cache *)malloc(sizeof(tex_cache));
  if (!c) { SDL_DestroyTexture(tex); return NULL; }
  c->tex = tex;
  SDL_QueryTexture(tex, NULL, NULL, &c->w, &c->h);
  snprintf(c->key, sizeof(c->key), "%s", keybuf);
  c->next = g_cache_head;
  g_cache_head = c;
  g_cache_count++;
  if (w) *w = c->w;
  if (h) *h = c->h;
  return tex;
}

static void cache_clear(void) {
  tex_cache *c = g_cache_head;
  while (c) {
    tex_cache *next = c->next;
    SDL_DestroyTexture(c->tex);
    free(c);
    c = next;
  }
  g_cache_head = NULL;
  g_cache_count = 0;
}

/* ---- Rendering primitives ------------------------------------------------- */

int ui_draw_text(const char *str, int x, int y, pp_color color) {
  if (!g_ren || !g_font) return -1;
  int w = 0, h = 0;
  SDL_Texture *tex = get_text_texture(str, color, &w, &h);
  if (!tex) return -1;
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
  TTF_SizeUTF8(g_font, str, &w, &h);
  return w;
}

/* ---- Screen stack -------------------------------------------------------- */

void ui_push(pp_screen *s) {
  if (!s) return;
  if (g_stack_count >= MAX_SCREENS) {
    LOGE("ui_push: stack overflow, leaking screen");
    return;
  }
  g_stack[++g_stack_top] = s;
  g_stack_count = g_stack_top + 1;
}

void ui_pop(void) {
  if (g_stack_top < 0) return;
  pp_screen *s = g_stack[g_stack_top];
  g_stack[g_stack_top] = NULL;
  g_stack_top--;
  if (g_stack_top < 0) g_stack_top = -1;
  if (s && g_pop_count < MAX_SCREENS) g_pop_queue[g_pop_count++] = s;
}

/* Cancel and free deferred screens. Called at end of frame.
 * Screens whose workers are still in-flight are kept alive for another frame. */
static void ui_finalize_pops(void) {
  int i, kept = 0;
  for (i = 0; i < g_pop_count; i++) {
    pp_screen *s = g_pop_queue[i];
    if (!s) continue;
    if (s->cancel) s->cancel(s);
    if (s->is_busy && s->is_busy(s)) {
      g_pop_queue[kept++] = s;  /* worker still running — wait */
    } else {
      if (s->destroy) s->destroy(s);
      free(s);
    }
  }
  g_pop_count = kept;
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
  return 0;
}

void ui_set_ini_path(const char *path) {
  if (path) snprintf(g_ini_path, sizeof(g_ini_path), "%s", path);
}
const char *ui_ini_path(void) { return g_ini_path[0] ? g_ini_path : "pocketplex.ini"; }

void ui_set_server(pp_server *srv) {
  if (g_server_owned && g_current_server) {
    pp_servers_free(g_current_server, 1);
    g_current_server = NULL;
    g_server_owned = 0;
  }
  if (srv) {
    pp_server *copy = (pp_server *)calloc(1, sizeof(pp_server));
    if (copy) {
      copy->url = srv->url ? strdup(srv->url) : NULL;
      copy->token = srv->token ? strdup(srv->token) : NULL;
      copy->client_id = srv->client_id ? strdup(srv->client_id) : NULL;
      g_current_server = copy;
      g_server_owned = 1;
    }
  } else {
    g_current_server = NULL;
    g_server_owned = 0;
  }
}

void ui_set_auth_token(const char *token) {
  if (token) snprintf(g_auth_token, sizeof(g_auth_token), "%s", token);
}
const char *ui_get_auth_token(void) { return g_auth_token[0] ? g_auth_token : NULL; }

void ui_push_screen(pp_screen_id id) {
  pp_screen *s = NULL;
  switch (id) {
  case SCREEN_LINK:     s = screen_link_create(); break;
  case SCREEN_SERVERS:  s = screen_servers_create(); break;
  case SCREEN_HOME:     s = screen_home_create(); break;
  case SCREEN_SETTINGS: s = screen_settings_create(); break;
  case SCREEN_DETAIL:
  case SCREEN_LIST:     s = screen_list_create_key(NULL, "Library"); break;
  case SCREEN_TOAST:
  case SCREEN_COUNT:    break;
  }
  if (s) ui_push(s);
}

pp_server *ui_current_server(void) {
  return g_current_server;
}

void ui_quit(void) {
  while (g_stack_top >= 0) ui_pop();
  /* Flush any remaining deferred pops */
  for (int i = 0; i < g_pop_count; i++) {
    pp_screen *s = g_pop_queue[i];
    if (!s) continue;
    if (s->cancel) s->cancel(s);
    if (s->is_busy && s->is_busy(s)) {
      /* Worker still running at shutdown — leak is acceptable here. */
      continue;
    }
    if (s->destroy) s->destroy(s);
    free(s);
  }
  g_pop_count = 0;
  cache_clear();
  if (g_server_owned && g_current_server) {
    pp_servers_free(g_current_server, 1);
    g_current_server = NULL;
    g_server_owned = 0;
  }
  if (g_font_bold && g_font_bold != g_font) TTF_CloseFont(g_font_bold);
  if (g_font) TTF_CloseFont(g_font);
  g_font = g_font_bold = NULL;
}

void ui_render_toast(void) {
  if (g_toast_ms_left <= 0) return;
  ui_fill_rect(0, PP_SCREEN_H - 48, PP_SCREEN_W, 48, PP_COLOR(0, 0, 0));
  ui_draw_rect(0, PP_SCREEN_H - 48, PP_SCREEN_W, 48, PP_COLOR_FG);
  ui_draw_text(g_toast_msg, PP_MARGIN_L, PP_SCREEN_H - 36, PP_COLOR_FG);
}

void ui_set_smoke_scroll(int on) { g_smoke_scroll = on; }
int  ui_is_smoke_scroll(void) { return g_smoke_scroll; }

void ui_set_smoke_walk(int on) { g_smoke_walk = on; }
int  ui_is_smoke_walk(void) { return g_smoke_walk; }

void ui_run(void) {
  int running = 1;
  Uint32 start_tick = SDL_GetTicks();
  long exit_after = ui_get_exit_after_ms();
  Uint32 last_tick = start_tick;
  Uint32 fps_check = start_tick;
  int frame_count = 0;

  if (g_smoke_scroll) {
    g_smoke_start = start_tick;
    g_smoke_phase = 0;
  }
  if (g_smoke_walk) {
    g_smoke_start = start_tick;
    g_smoke_phase = 0;
  }

  /* Initial screen is pushed by main.c (Link or Home). */
  while (running && g_stack_top >= 0) {
    Uint32 now = SDL_GetTicks();
    Uint32 frame_start = now;
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
      LOGI("fps: %d (stack=%d, loading=%d, max_frame=%u ms)",
           frame_count, g_stack_count,
           current_screen() ? current_screen()->loading : 0, g_max_frame_ms);
      frame_count = 0;
      fps_check = now;
    }

    /* Smoke scroll: auto-navigate through screens, building stack >= 3 */
    if (g_smoke_scroll) {
      pp_screen *cur = current_screen();
      Uint32 t = now - g_smoke_start;
      switch (g_smoke_phase) {
      case 0: /* Start: push Servers on top of Link (stack grows) */
        if (t >= 100) {
          ui_push(screen_servers_create());
          g_smoke_phase = 1;
        }
        break;
      case 1: /* Servers loading -> push Home (stack grows) */
        if (cur && !cur->loading) {
          ui_push(screen_home_create());
          g_smoke_phase = 2;
        }
        break;
      case 2: /* Home loading -> push List with TV key (stack grows) */
        if (cur && !cur->loading) {
          ui_push(screen_list_create_key("tv", "TV Shows"));
          g_smoke_phase = 3;
        }
        break;
      case 3: /* List loading -> ready to scroll */
        if (cur && !cur->loading) {
          LOGI("smoke: reached 2000-item list, stack=%d", g_stack_count);
          g_smoke_phase = 4;
          g_smoke_start = now;
        }
        break;
      case 4: /* Hold DOWN for 3 s on the 2000-item list */
        if (cur && !cur->loading) {
          if (cur->handle_button) cur->handle_button(cur, BTN_DOWN);
          g_smoke_scroll_count++;
        }
        if (t >= 3000) {
          LOGI("smoke: scrolled %d DOWN presses, stack=%d, max_frame=%u ms",
               g_smoke_scroll_count, g_stack_count, g_max_frame_ms);
          running = 0;
        }
        break;
      }
    }

    /* Smoke walk: auto-navigate Home→Library(TV)→Show→Season→Episode */
    if (g_smoke_walk) {
      pp_screen *cur = current_screen();
      Uint32 t = now - g_smoke_start;
      switch (g_smoke_phase) {
      case 0: /* Wait for Home to load, log sections, then go to TV Shows */
        if (cur && !cur->loading) {
          if (cur->log_titles) cur->log_titles(cur, 3);
          ui_pop();
          ui_push(screen_list_create_key("2", "TV Shows"));
          g_smoke_phase = 1;
          g_smoke_start = now;
        }
        break;
      case 1: /* Wait for TV Shows List to load, log, press A on first show */
        if (cur && !cur->loading && t >= 200) {
          LOGI("walk: Level 1 — Library (TV Shows)");
          if (cur->log_titles) cur->log_titles(cur, 3);
          if (cur->handle_button) cur->handle_button(cur, BTN_A);
          g_smoke_phase = 2;
          g_smoke_start = now;
        }
        break;
      case 2: /* Wait for Show List, log, press A on first season */
        if (cur && !cur->loading && t >= 200) {
          LOGI("walk: Level 2 — Show");
          if (cur->log_titles) cur->log_titles(cur, 3);
          if (cur->handle_button) cur->handle_button(cur, BTN_A);
          g_smoke_phase = 3;
          g_smoke_start = now;
        }
        break;
      case 3: /* Wait for Season List, log, press A on first episode */
        if (cur && !cur->loading && t >= 200) {
          LOGI("walk: Level 3 — Season");
          if (cur->log_titles) cur->log_titles(cur, 3);
          if (cur->handle_button) cur->handle_button(cur, BTN_A);
          g_smoke_phase = 4;
          g_smoke_start = now;
        }
        break;
      case 4: /* Wait for Detail, log, exit */
        if (t >= 200) {
          LOGI("walk: Level 4 — Episode");
          if (cur->log_titles) cur->log_titles(cur, 1);
          LOGI("walk: complete — Library → Show → Season → Episode");
          running = 0;
        }
        break;
      }
    }

    pp_screen *s = current_screen();
    if (!s) break;

    SDL_SetRenderDrawColor(g_ren, 0x18, 0x18, 0x1c, 0xff);
    SDL_RenderClear(g_ren);

    if (s->render) s->render(s);
    ui_render_toast();

    plat_present();

    Uint32 frame_end = SDL_GetTicks();
    Uint32 frame_ms = frame_end - frame_start;
    if (frame_ms > g_max_frame_ms) g_max_frame_ms = frame_ms;
    if (frame_ms > 50) {
      LOGI("WARNING: frame time %u ms > 50 ms (loading=%d)", frame_ms, s->loading);
    }

    pp_btn btn = plat_poll_button();
    if (btn == BTN_MENU) { running = 0; break; }
    if (btn != BTN_NONE && s->handle_button) s->handle_button(s, btn);

    /* Flush deferred pops: cancel workers, free screens whose workers are done. */
    ui_finalize_pops();
  }

  if (g_smoke_scroll) {
    LOGI("smoke done: total max frame time = %u ms (must be <50)", g_max_frame_ms);
  }
}
