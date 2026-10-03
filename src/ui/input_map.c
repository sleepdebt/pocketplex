/* input_map.c: keyboard to pp_btn mapping for desktop. Tested without SDL. */
#include "ui/input_map.h"

pp_keymap pp_default_keys[] = {
  { 0x28, BTN_DOWN },     /* SDLK_DOWN   */
  { 0x26, BTN_UP },       /* SDLK_UP     */
  { 0x25, BTN_LEFT },     /* SDLK_LEFT   */
  { 0x27, BTN_RIGHT },    /* SDLK_RIGHT  */
  { 0x0D, BTN_A },        /* SDLK_RETURN */
  { 0x08, BTN_B },        /* SDLK_BACKSPACE */
  { 0x7A, BTN_X },        /* SDLK_z      */
  { 0x78, BTN_Y },        /* SDLK_x      */
  { 0x21, BTN_L1 },       /* SDLK_PAGEUP */
  { 0x22, BTN_R1 },       /* SDLK_PAGEDOWN */
  { 0x73, BTN_START },    /* SDLK_s */
  { 0x70, BTN_SELECT },   /* SDLK_p */
  { 0x1B, BTN_MENU },     /* SDLK_ESCAPE */
  { 0x61, BTN_A },        /* SDLK_a — alternate A */
  { 0x00, BTN_NONE },
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
