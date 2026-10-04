/* ui.c: screen stack, font/text rendering, main event+render loop. */
#include "ui.h"
#include "ui/ui_platform.h"
#include "config/config.h"
#include "ui/worker.h"
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

static pp_server *g_current_server = NULL;  /* owned deep copy */
static char g_auth_token[256] = {0};
static char g_ini_path[512] = {0};

/* ---- Smoke scroll + max frame tracking ------------------------------------ */
static int g_smoke_scroll = 0;
static int g_smoke_walk = 0;
static int g_smoke_phase = 0;
static Uint32 g_smoke_start = 0;
static int g_smoke_scroll_count = 0;
static Uint32 g_max_frame_ms = 0;
static char g_smoke_play_key[256] = {0};
static long g_smoke_play_ms = 0;
static int g_smoke_play_saw_player = 0;

static pp_screen *g_stack[MAX_SCREENS];
static int g_stack_top = -1;
static int g_stack_count = 0;

/* Deferred pop queue: popped screens are destroyed at end of frame, so a
 * handler can still read its own data after calling ui_pop(). Workers never
 * need the screen alive: each request is refcounted (see worker.h). */
static pp_screen *g_pop_queue[2 * MAX_SCREENS];
static int g_pop_count = 0;

static char g_toast_msg[MAX_TOAST_LEN] = {0};
static int g_toast_ms_left = 0;
static const int TOAST_LIFETIME_MS = 3000;

/* ---- Text texture cache (bounded LRU, keyed by text) --------------------- */
/* Text is rendered once in white; ui_draw_text applies the colour with colour
 * and alpha mod, so a row changing colour (selection) reuses its texture. */

#define CACHE_MAX 64

typedef struct tex_cache {
  char *key;              /* owned copy of the text */
  SDL_Texture *tex;
  int w, h;
  struct tex_cache *next;
} tex_cache;

static tex_cache *g_cache_head = NULL;
static int g_cache_count = 0;

static void cache_entry_free(tex_cache *c) {
  SDL_DestroyTexture(c->tex);
  free(c->key);
  free(c);
}

static void cache_move_to_front(tex_cache *prev, tex_cache *c) {
  if (!prev) return;  /* already the head */
  prev->next = c->next;
  c->next = g_cache_head;
  g_cache_head = c;
}

static void cache_evict_lru(void) {
  if (g_cache_count < CACHE_MAX || !g_cache_head) return;
  tex_cache *prev = NULL, *c = g_cache_head;
  while (c->next) { prev = c; c = c->next; }
  if (prev) prev->next = NULL; else g_cache_head = NULL;
  cache_entry_free(c);
  g_cache_count--;
}

static SDL_Texture *get_text_texture(const char *str, int *w, int *h) {
  tex_cache *prev = NULL, *c;
  for (c = g_cache_head; c; prev = c, c = c->next) {
    if (strcmp(c->key, str) == 0) {
      cache_move_to_front(prev, c);
      *w = c->w;
      *h = c->h;
      return c->tex;
    }
  }

  SDL_Color white = { 0xff, 0xff, 0xff, 0xff };
  SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font, str, white);
  if (!surf) return NULL;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(g_ren, surf);
  SDL_FreeSurface(surf);
  if (!tex) return NULL;

  cache_evict_lru();
  c = (tex_cache *)calloc(1, sizeof(tex_cache));
  if (c) c->key = strdup(str);
  if (!c || !c->key) { free(c); SDL_DestroyTexture(tex); return NULL; }
  c->tex = tex;
  SDL_QueryTexture(tex, NULL, NULL, &c->w, &c->h);
  c->next = g_cache_head;
  g_cache_head = c;
  g_cache_count++;
  *w = c->w;
  *h = c->h;
  return tex;
}

static void cache_clear(void) {
  tex_cache *c = g_cache_head;
  while (c) {
    tex_cache *next = c->next;
    cache_entry_free(c);
    c = next;
  }
  g_cache_head = NULL;
  g_cache_count = 0;
}

/* ---- Rendering primitives ------------------------------------------------- */

int ui_draw_text(const char *str, int x, int y, pp_color color) {
  if (!g_ren || !g_font) return -1;
  int w = 0, h = 0;
  if (!str || !str[0]) return 0;
  SDL_Texture *tex = get_text_texture(str, &w, &h);
  if (!tex) return -1;
  SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(tex, color.a);
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
    LOGE("ui_push: stack full, dropping screen");
    if (s->destroy) s->destroy(s);
    free(s);
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
  g_stack_count = g_stack_top + 1;
  if (!s) return;
  if (g_pop_count < (int)(sizeof(g_pop_queue) / sizeof(g_pop_queue[0])))
    g_pop_queue[g_pop_count++] = s;
  else
    LOGE("ui_pop: pop queue full, leaking screen");
}

/* Destroy screens popped this frame. Called at end of frame. */
static void ui_finalize_pops(void) {
  int i;
  for (i = 0; i < g_pop_count; i++) {
    pp_screen *s = g_pop_queue[i];
    if (s->destroy) s->destroy(s);
    free(s);
  }
  g_pop_count = 0;
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
  return 0;
}

void ui_set_ini_path(const char *path) {
  if (path) snprintf(g_ini_path, sizeof(g_ini_path), "%s", path);
}
const char *ui_ini_path(void) { return g_ini_path[0] ? g_ini_path : "pocketplex.ini"; }

static void server_free(pp_server *srv) {
  if (!srv) return;
  free(srv->url);
  if (srv->token) memset(srv->token, 0, strlen(srv->token));
  free(srv->token);
  free(srv->client_id);
  free(srv);
}

void ui_set_server(pp_server *srv) {
  server_free(g_current_server);
  g_current_server = NULL;
  if (!srv) return;
  pp_server *copy = (pp_server *)calloc(1, sizeof(pp_server));
  if (!copy) return;
  copy->url = srv->url ? strdup(srv->url) : NULL;
  copy->token = srv->token ? strdup(srv->token) : NULL;
  copy->client_id = srv->client_id ? strdup(srv->client_id) : NULL;
  g_current_server = copy;
}

void ui_set_auth_token(const char *token) {
  if (token) snprintf(g_auth_token, sizeof(g_auth_token), "%s", token);
  else memset(g_auth_token, 0, sizeof(g_auth_token));
}
const char *ui_get_auth_token(void) { return g_auth_token[0] ? g_auth_token : NULL; }

void ui_sign_out(void) {
  ui_set_auth_token(NULL);
  ui_set_server(NULL);
  while (g_stack_top >= 0) ui_pop();
  ui_push(screen_link_create());
}

void ui_push_screen(pp_screen_id id) {
  pp_screen *s = NULL;
  switch (id) {
  case SCREEN_LINK:     s = screen_link_create(); break;
  case SCREEN_SERVERS:  s = screen_servers_create(); break;
  case SCREEN_HOME:     s = screen_home_create(); break;
  case SCREEN_SETTINGS: s = screen_settings_create(); break;
  case SCREEN_DETAIL:
  case SCREEN_LIST:     s = screen_list_create_key(NULL, "Library"); break;
  case SCREEN_PLAYER:
  case SCREEN_TOAST:
  case SCREEN_COUNT:    break;
  }
  if (s) ui_push(s);
}

pp_server *ui_current_server(void) {
  return g_current_server;
}

int ui_quit(void) {
  while (g_stack_top >= 0) ui_pop();
  ui_finalize_pops();
  /* Released requests are freed by their workers when they finish; give any
   * still blocked in the network a moment so nothing is left at exit. */
  int idle = worker_wait_idle(3000);
  if (idle != 0)
    LOGW("ui_quit: %d request(s) still in flight at exit", worker_live_count());
  cache_clear();
  ui_set_server(NULL);
  ui_set_auth_token(NULL);
  if (g_font_bold && g_font_bold != g_font) TTF_CloseFont(g_font_bold);
  if (g_font) TTF_CloseFont(g_font);
  g_font = g_font_bold = NULL;
  return idle;
}

int g_spinner_frame = 0;

void ui_draw_spinner(int x, int y, int frame) {
  const char spinner[] = "|/-\\";
  char buf[16];
  snprintf(buf, sizeof(buf), "Loading %c", spinner[frame % 4]);
  ui_draw_text(buf, x, y, PP_COLOR_FG);
}

void ui_render_toast(void) {
  if (g_toast_ms_left <= 0) return;
  ui_fill_rect(0, PP_SCREEN_H - 48, PP_SCREEN_W, 48, PP_COLOR(0, 0, 0));
  ui_draw_rect(0, PP_SCREEN_H - 48, PP_SCREEN_W, 48, PP_COLOR_FG);
  ui_draw_text(g_toast_msg, PP_MARGIN_L, PP_SCREEN_H - 36, PP_COLOR_FG);
}

void ui_set_smoke_scroll(int on) { g_smoke_scroll = on; }
int  ui_is_smoke_scroll(void) { return g_smoke_scroll; }

void ui_set_smoke_play(const char *key, long play_ms) {
  snprintf(g_smoke_play_key, sizeof(g_smoke_play_key), "%s", key ? key : "");
  g_smoke_play_ms = play_ms;
}
const char *ui_smoke_play_key(void) { return g_smoke_play_key; }
long ui_smoke_play_ms(void) { return g_smoke_play_key[0] ? g_smoke_play_ms : 0; }

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
  if (g_smoke_walk || ui_smoke_play_ms() > 0) {
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
        if (cur && t >= 200) {
          LOGI("walk: Level 4 — Episode");
          if (cur->log_titles) cur->log_titles(cur, 1);
          LOGI("walk: complete — Library → Show → Season → Episode");
          running = 0;
        }
        break;
      }
    }

    /* Smoke play: List(key) -> A (Detail) -> A (play from start) -> back -> exit */
    if (ui_smoke_play_ms() > 0) {
      pp_screen *cur = current_screen();
      Uint32 t = now - g_smoke_start;
      switch (g_smoke_phase) {
      case 0:
        if (cur && cur->id == SCREEN_LIST && !cur->loading && t >= 200) {
          if (cur->log_titles) cur->log_titles(cur, 1);
          if (cur->handle_button) cur->handle_button(cur, BTN_A);
          g_smoke_phase = 1;
          g_smoke_start = now;
        }
        break;
      case 1:
        if (cur && cur->id == SCREEN_DETAIL && t >= 200) {
          if (cur->handle_button) cur->handle_button(cur, BTN_A);
          g_smoke_phase = 2;
        }
        break;
      case 2:
        if (cur && cur->id == SCREEN_PLAYER) g_smoke_play_saw_player = 1;
        if (cur && cur->id == SCREEN_DETAIL && g_smoke_play_saw_player) {
          LOGI("play: back on Detail, done");
          running = 0;
        }
        break;
      }
    }

    pp_screen *s = current_screen();
    if (!s) break;

    if (s->no_present) {
      /* An external player owns the display: no clear, no present. */
      if (s->render) s->render(s);
      SDL_Delay(15);
    } else {
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
    }

    pp_btn btn = plat_poll_button();
    if (btn == BTN_MENU) { running = 0; break; }
    if (btn != BTN_NONE && s->handle_button) s->handle_button(s, btn);

    /* Destroy screens popped this frame. */
    ui_finalize_pops();
  }

  if (g_smoke_scroll) {
    LOGI("smoke done: total max frame time = %u ms (must be <50)", g_max_frame_ms);
  }
}
