/* input_map.c: keyboard to pp_btn mapping for desktop. Tested without SDL. */
#include "ui/input_map.h"

#include <ctype.h>

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
static pp_pad_layout g_layout = PP_LAYOUT_POSITIONAL;

/* Case-insensitive substring match (ASCII). */
static int has_word(const char *hay, const char *needle) {
  for (; *hay; hay++) {
    const char *h = hay, *n = needle;
    while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
    if (!*n) return 1;
  }
  return 0;
}

pp_pad_layout pp_pad_layout_for(const char *name, int sdl_label_mapped, int handheld_build) {
  static const char *const nintendo[] = { "anbernic", "rg35xx", "rg40xx", "rg28xx", "rgcube",
                                          "miyoo", NULL };
  static const char *const positional[] = { "xbox", "x-box", "playstation", "dualshock",
                                            "dualsense", "ps4", "ps5", NULL };
  if (sdl_label_mapped) return PP_LAYOUT_POSITIONAL;  /* SDL already applied labels */
  if (name) {
    for (int i = 0; positional[i]; i++) if (has_word(name, positional[i])) return PP_LAYOUT_POSITIONAL;
    for (int i = 0; nintendo[i]; i++) if (has_word(name, nintendo[i])) return PP_LAYOUT_NINTENDO;
  }
  return handheld_build ? PP_LAYOUT_NINTENDO : PP_LAYOUT_POSITIONAL;
}

void pp_pad_set_layout(pp_pad_layout layout) { g_layout = layout; }
pp_pad_layout pp_pad_get_layout(void) { return g_layout; }
const char *pp_pad_layout_name(pp_pad_layout layout) {
  return layout == PP_LAYOUT_NINTENDO ? "nintendo" : "positional";
}

void ui_set_swap_ab(int swap) { g_swap_ab = swap ? 1 : 0; }
int  ui_is_swap_ab(void) { return g_swap_ab; }

static pp_btn swap_ab(pp_btn btn) {
  if (g_swap_ab) {
    if (btn == BTN_A) return BTN_B;
    if (btn == BTN_B) return BTN_A;
  }
  return btn;
}

pp_btn pp_pad_button_to_btn(int pad_button) {
  pp_btn b;
  switch (pad_button) {
  /* Nintendo layout: east (SDL B) is labelled A, north (SDL Y) is labelled X. */
  case PP_PAD_A:             b = g_layout == PP_LAYOUT_NINTENDO ? BTN_B : BTN_A; break;
  case PP_PAD_B:             b = g_layout == PP_LAYOUT_NINTENDO ? BTN_A : BTN_B; break;
  case PP_PAD_X:             b = g_layout == PP_LAYOUT_NINTENDO ? BTN_Y : BTN_X; break;
  case PP_PAD_Y:             b = g_layout == PP_LAYOUT_NINTENDO ? BTN_X : BTN_Y; break;
  case PP_PAD_BACK:          b = BTN_SELECT; break;
  case PP_PAD_GUIDE:         b = BTN_MENU; break;
  case PP_PAD_START:         b = BTN_START; break;
  case PP_PAD_LEFTSHOULDER:  b = BTN_L1; break;
  case PP_PAD_RIGHTSHOULDER: b = BTN_R1; break;
  case PP_PAD_DPAD_UP:       b = BTN_UP; break;
  case PP_PAD_DPAD_DOWN:     b = BTN_DOWN; break;
  case PP_PAD_DPAD_LEFT:     b = BTN_LEFT; break;
  case PP_PAD_DPAD_RIGHT:    b = BTN_RIGHT; break;
  default:                   return BTN_NONE;
  }
  return swap_ab(b);
}

const char *pp_btn_name(pp_btn b) {
  static const char *const names[] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2", "START", "SELECT", "MENU" };
  return ((int)b >= 0 && b < BTN_NONE) ? names[b] : "none";
}

pp_btn pp_keycode_to_btn(int sdl_keycode) {
  int i;
  pp_btn btn = BTN_NONE;
  for (i = 0; pp_default_keys[i].btn != BTN_NONE; i++) {
    if (pp_default_keys[i].sdl_key == sdl_keycode) {
      btn = pp_default_keys[i].btn;
      break;
    }
  }
  return swap_ab(btn);
}
