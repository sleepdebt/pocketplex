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

/* Swap A/B buttons (for configurable mapping per the spec). */
void ui_set_swap_ab(int swap);
int  ui_is_swap_ab(void);

#endif /* PP_INPUT_MAP_H */
