/* ui/input_map.h: keyboard -> pp_btn mapping.
 * No SDL dependency so tests can use it directly.
 */
#ifndef PP_INPUT_MAP_H
#define PP_INPUT_MAP_H

#include "platform/platform.h"
#include <stddef.h>

/* SDL_Keycode values for common keys, used by both CORE (tests, no SDL)
 * and APP (with SDL). Mirrors SDLK_* from SDL_keycode.h. */
#define PP_SDLK_DOWN       0x40000051
#define PP_SDLK_UP         0x40000052
#define PP_SDLK_LEFT       0x40000050
#define PP_SDLK_RIGHT      0x4000004F
#define PP_SDLK_PAGEUP     0x4000004B
#define PP_SDLK_PAGEDOWN   0x4000004E
#define PP_SDLK_RETURN     0x0D
#define PP_SDLK_BACKSPACE  0x08
#define PP_SDLK_ESCAPE     0x1B

typedef struct {
  int sdl_key;    /* SDL_Keycode value */
  pp_btn btn;
} pp_keymap;

extern pp_keymap pp_default_keys[];

/* Map an SDL_Keycode to a pp_btn. Returns BTN_NONE for unmapped keys.
 * Honors ui_set_swap_ab() for firmware A/B position differences. */
pp_btn pp_keycode_to_btn(int sdl_keycode);

/* SDL_GameControllerButton values (checked against SDL in sdl2.c). */
#define PP_PAD_A              0
#define PP_PAD_B              1
#define PP_PAD_X              2
#define PP_PAD_Y              3
#define PP_PAD_BACK           4
#define PP_PAD_GUIDE          5
#define PP_PAD_START          6
#define PP_PAD_LEFTSTICK      7
#define PP_PAD_RIGHTSTICK     8
#define PP_PAD_LEFTSHOULDER   9
#define PP_PAD_RIGHTSHOULDER 10
#define PP_PAD_DPAD_UP       11
#define PP_PAD_DPAD_DOWN     12
#define PP_PAD_DPAD_LEFT     13
#define PP_PAD_DPAD_RIGHT    14

/* Face-button layout of the active controller. SDL reports face buttons by
 * position (Xbox: A = south). Nintendo-layout handhelds (Anbernic, Miyoo)
 * print A on the east button, so their labels need the swap. */
typedef enum { PP_LAYOUT_POSITIONAL, PP_LAYOUT_NINTENDO } pp_pad_layout;

/* Pick the layout for a controller. sdl_label_mapped: SDL already maps it by
 * label (Switch controllers with USE_BUTTON_LABELS). handheld_build: sp/mmp,
 * where an unknown controller is the built-in Nintendo-layout pad. */
pp_pad_layout pp_pad_layout_for(const char *name, int sdl_label_mapped, int handheld_build);
void pp_pad_set_layout(pp_pad_layout layout);
pp_pad_layout pp_pad_get_layout(void);
const char *pp_pad_layout_name(pp_pad_layout layout);

/* Map a GameController button to a pp_btn: layout first, then the user's
 * swap_ab on top. BTN_NONE if unmapped. */
pp_btn pp_pad_button_to_btn(int pad_button);

/* Short name for logs ("A", "MENU", "none"). */
const char *pp_btn_name(pp_btn b);

/* Swap A/B buttons (for configurable mapping per the spec). */
void ui_set_swap_ab(int swap);
int  ui_is_swap_ab(void);

/* ---- Display layout ----------------------------------------------------- */
/* The UI is designed at 640x480; on larger panels (the 1024x768 Brick Pro)
 * everything scales by one factor, h/480. Set once from the plat_init size
 * (ui_init does this). Pure, no SDL, so tests can drive it. */

/* Set the real display size; w/h <= 0 resets to the 640x480 design size. */
void   pp_layout_set_display(int w, int h);
double pp_layout_scale(void);        /* h / 480.0 */
int    pp_layout_px(int px);         /* design pixel -> display pixel, rounded */
int    pp_layout_w(void);
int    pp_layout_h(void);
int    pp_layout_margin_l(void);
int    pp_layout_margin_r(void);
int    pp_layout_header_h(void);
int    pp_layout_footer_h(void);
int    pp_layout_line_h(void);
int    pp_layout_visible(void);      /* list rows between header and footer */
int    pp_layout_font_pt(void);      /* 20 pt at 480p */
int    pp_layout_font_bold_pt(void); /* 22 pt at 480p */

/* "WxH" for PP_FAKE_DISPLAY (desktop layout checks), bounded to
 * 320x240..4096x4096. 0 = parsed into w/h, -1 = malformed or out of bounds. */
int pp_parse_display_size(const char *s, int *w, int *h);

/* ---- tg5040 raw evdev: Brick Pro pad codes ------------------------------- */
/* SDL 2.26 (TrimUI SDK) does not enumerate the built-in "TRIMUI Player1" pad,
 * so the tg5040 UI reads /dev/input/event* directly (decision:
 * ops/reviews/tg-0-input.md; codes measured by tg-0, LOG.md 2026-10-18).
 * Raw evdev codes, NOT SDL indices. Face buttons follow the printed labels
 * (the printed-A button emits 304/BTN_SOUTH); the user's swap_ab applies on
 * top, exactly like the SDL gamepad path. */

/* EV_KEY code -> button. Mapped: A=304 B=305 X=307 Y=308 L1=310 R1=311
 * Select=314 Start=315 Menu=316. Deliberately BTN_NONE: L3=317/R3=318 (no
 * pp_btn for them) and KEY 59/60 (the two assignable buttons - owner hasn't
 * chosen actions yet, D-input-2). Honors ui_set_swap_ab for A/B. */
pp_btn pp_tg5040_key_to_btn(int code);

/* D-pad hat axes: ABS 16 (X) / ABS 17 (Y), kernel sign convention
 * (-1 = left/up, +1 = right/down; 0 = centred, |value| > 1 ignored). */
pp_btn pp_tg5040_hat_to_btn(int axis, int value);

/* Analog trigger axes: ABS 2 -> BTN_L2, ABS 5 -> BTN_R2, else BTN_NONE.
 * (The press threshold is applied by the evdev reader, not here.) */
pp_btn pp_tg5040_trigger_to_btn(int axis);

#endif /* PP_INPUT_MAP_H */
