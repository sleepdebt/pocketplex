/* ui/ui.h: PocketPlex UI shell.
 * Screen-stack + text-rendering primitives over the platform.h backend.
 */
#ifndef PP_UI_H
#define PP_UI_H

#include "platform/platform.h"
#include "plex/plex.h"
#include "ui/input_map.h"
#include <stddef.h>

/* ---- Screen stack ------------------------------------------------------- */

typedef enum {
  SCREEN_LINK,
  SCREEN_SERVERS,
  SCREEN_HOME,
  SCREEN_LIST,
  SCREEN_DETAIL,
  SCREEN_SETTINGS,
  SCREEN_TOAST,
  SCREEN_COUNT
} pp_screen_id;

typedef struct pp_screen pp_screen;
struct pp_screen {
  pp_screen_id id;
  int loading;       /* 1 while an async request is pending */
  void (*render)(pp_screen *self);
  void (*handle_button)(pp_screen *self, pp_btn);
  void (*log_titles)(pp_screen *self, int n);  /* optional: log first n titles */
  void (*cancel)(pp_screen *self);             /* cancel in-flight workers */
  int (*is_busy)(pp_screen *self);              /* 1 if a worker is still in-flight */
  void (*destroy)(pp_screen *self);
  void *data;
};

/* Initialise SDL_ttf, push the root screen, etc. 0 = ok. */
int  ui_init(void);
/* Runs the event+render loop until the stack is empty (or BTN_MENU).
 * If ui_set_exit_after_ms > 0, exits after that many ms (for CI/screenshots). */
void ui_run(void);
void ui_quit(void);

/* Auto-exit timeout (for --exit-after-ms). */
void ui_set_exit_after_ms(long ms);
long  ui_get_exit_after_ms(void);

/* Smoke test mode: auto-navigates Link→Servers→Home→List(2000),
 * holds DOWN for ~3s, logs fps. */
void ui_set_smoke_scroll(int on);
int  ui_is_smoke_scroll(void);

/* Smoke walk mode: auto-navigates Library→Show→Season→Episode on real server,
 * logging each level's first titles to prove the walk-through matches pp-cli. */
void ui_set_smoke_walk(int on);
int  ui_is_smoke_walk(void);

/* Push/pop screens on the global stack. */
void ui_push(pp_screen *s);
void ui_push_screen(pp_screen_id id);
void ui_pop(void);

/* Set the current server (from config or discovery). The pointer is stored
 * by reference; the caller retains ownership and lifetime. */
void ui_set_server(pp_server *srv);
pp_server *ui_current_server(void);

/* Auth token from PIN flow (used by Servers screen for discovery). */
void  ui_set_auth_token(const char *token);
const char *ui_get_auth_token(void);

/* INI path for config save after PIN auth. */
void  ui_set_ini_path(const char *path);
const char *ui_ini_path(void);

/* Toast a one-line message that auto-clears after a few seconds. */
void ui_toast(const char *msg);

/* ---- List view model (pure logic, testable) ------------------------------ */

typedef struct {
  pp_item *items;
  int count;
  int capacity;
} pp_list_model;

typedef struct {
  pp_list_model *model;
  int selected;       /* index into model->items */
  int scroll_top;     /* top visible index */
  int visible_count;  /* items shown per page (set at init) */
} pp_list_view;

void list_view_init(pp_list_view *lv, pp_list_model *model, int visible_count);
void list_view_handle_button(pp_list_view *lv, pp_btn btn);
int  list_view_page_up(pp_list_view *lv);
int  list_view_page_down(pp_list_view *lv);
void list_view_ensure_visible(pp_list_view *lv);

/* ---- Text helpers (rendering layer) -------------------------------------- */

typedef struct {
  unsigned char r, g, b, a;
} pp_color;

#define PP_COLOR(r,g,b) ((pp_color){(r),(g),(b),255})
#define PP_COLOR_BG     PP_COLOR(0x18,0x18,0x1c)
#define PP_COLOR_FG     PP_COLOR(0xe0,0xe0,0xe0)
#define PP_COLOR_SEL    PP_COLOR(0x00,0x99,0xff)
#define PP_COLOR_DIM    PP_COLOR(0x80,0x80,0x80)
#define PP_COLOR_RED    PP_COLOR(0xff,0x44,0x44)

/* Draw text at (x,y) in pixels. Returns 0 on success. */
int  ui_draw_text(const char *str, int x, int y, pp_color color);
void ui_fill_rect(int x, int y, int w, int h, pp_color color);
void ui_draw_rect(int x, int y, int w, int h, pp_color color);
void ui_draw_scrollbar(int x, int y, int h, int selected, int total);

/* Text width in pixels (for truncation/ellipsis). */
int  text_width_px(const char *str);

pp_server *ui_current_server(void);
void ui_render_toast(void);

/* Screen factory functions — declared here so ui.c can create the root screen. */
pp_screen *screen_link_create(void);
pp_screen *screen_servers_create(void);
pp_screen *screen_home_create(void);
pp_screen *screen_list_create(const pp_list *items, const char *title);
pp_screen *screen_list_create_key(const char *key, const char *title);
pp_screen *screen_detail_create(const pp_item *item);
pp_screen *screen_settings_create(void);

/* Layout constants for 640x480. */
#define PP_SCREEN_W 640
#define PP_SCREEN_H 480
#define PP_MARGIN_L 16
#define PP_MARGIN_R 16
#define PP_HEADER_H 40
#define PP_FOOTER_H 32
#define PP_LINE_H   26
#define PP_VISIBLE  ((PP_SCREEN_H - PP_HEADER_H - PP_FOOTER_H) / PP_LINE_H)

#endif /* PP_UI_H */