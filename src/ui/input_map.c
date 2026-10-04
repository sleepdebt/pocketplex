/* input_map.c: keyboard to pp_btn mapping for desktop. Tested without SDL. */
#include "ui/input_map.h"

pp_keymap pp_default_keys[] = {
  { PP_SDLK_DOWN,     BTN_DOWN },
  { PP_SDLK_UP,       BTN_UP },
  { PP_SDLK_LEFT,     BTN_LEFT },
  { PP_SDLK_RIGHT,    BTN_RIGHT },
  { PP_SDLK_RETURN,   BTN_A },
  { PP_SDLK_BACKSPACE, BTN_B },
  { 'z',              BTN_X },
  { 'x',              BTN_Y },
  { PP_SDLK_PAGEUP,   BTN_L1 },
  { PP_SDLK_PAGEDOWN, BTN_R1 },
  { 's',              BTN_START },
  { 'p',              BTN_SELECT },
  { PP_SDLK_ESCAPE,   BTN_MENU },
  { 'a',              BTN_A },
  { 0,                BTN_NONE },
};

static int g_swap_ab = 0;

void ui_set_swap_ab(int swap) { g_swap_ab = swap ? 1 : 0; }
int  ui_is_swap_ab(void) { return g_swap_ab; }

pp_btn pp_keycode_to_btn(int sdl_keycode) {
  int i;
  pp_btn btn = BTN_NONE;
  for (i = 0; pp_default_keys[i].btn != BTN_NONE; i++) {
    if (pp_default_keys[i].sdl_key == sdl_keycode) {
      btn = pp_default_keys[i].btn;
      break;
    }
  }
  if (g_swap_ab) {
    if (btn == BTN_A) return BTN_B;
    if (btn == BTN_B) return BTN_A;
  }
  return btn;
}
