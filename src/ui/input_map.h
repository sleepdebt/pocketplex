/* ui/input_map.h: keyboard -> pp_btn mapping.
 * No SDL dependency so tests can use it directly.
 */
#ifndef PP_INPUT_MAP_H
#define PP_INPUT_MAP_H

#include "platform/platform.h"
#include <stddef.h>

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
