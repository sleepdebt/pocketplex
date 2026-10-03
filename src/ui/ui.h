/* ui/ui.h: PocketPlex UI shell.
 * Screen-stack + text-rendering primitives over the platform.h backend.
 */
#ifndef PP_UI_H
#define PP_UI_H

#include "platform/platform.h"
#include "plex/plex.h"
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
  void (*render)(pp_screen *self);
  void (*handle_button)(pp_screen *self, pp_btn);
  void (*destroy)(pp_screen *self);
  void *data;
};

/* Initialise SDL_ttf, push the root screen, etc. 0 = ok. */
int  ui_init(void);
/* Runs the event+render loop until the stack is empty (or BTN_MENU). */
void ui_run(void);
void ui_quit(void);

/* Push/pop screens on the global stack. */
void ui_push(pp_screen *s);
void ui_pop(void);

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

/* ---- Input mapping (desktop keyboard → pp_btn, testable) ----------------- */

typedef struct {
  int sdl_key;    /* SDL_Keycode value */
  pp_btn btn;
} pp_keymap;

extern pp_keymap pp_default_keys[];
pp_btn pp_keycode_to_btn(int sdl_keycode);

/* ---- Text helpers (rendering layer) -------------------------------------- */

typedef struct {
  int w, h;
} pp_vec2;

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
