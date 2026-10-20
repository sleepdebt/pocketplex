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

/* ---- tg5040 raw evdev: Brick Pro pad (TRIMUI Player1) -------------------- */
/* Codes measured by tg-0 on the device (ops/LOG.md 2026-10-18). Do NOT reuse
 * the SP codes: Y is 308 (BTN_WEST) here vs 306 (BTN_C) on the SP, and
 * Select/Start/Menu are 314/315/316 vs the SP's 310/311/312. */
static const struct { int code; pp_btn btn; } pp_tg5040_keys[] = {
  { 304, BTN_B },      /* BTN_SOUTH: the bottom button, printed B */
  { 305, BTN_A },      /* BTN_EAST: the right button, printed A */
  { 307, BTN_Y },      /* BTN_NORTH: the left button, printed Y (measured 2026-10-19) */
  { 308, BTN_X },      /* BTN_WEST: the top button, printed X (measured 2026-10-19) */
  { 310, BTN_L1 },     /* BTN_TL */
  { 311, BTN_R1 },     /* BTN_TR */
  { 314, BTN_SELECT }, /* BTN_SELECT */
  { 315, BTN_START },  /* BTN_START */
  { 316, BTN_MENU },   /* BTN_MODE: the only quit key */
  /* 317/318 (L3/R3) and KEY 59/60 (assignable buttons) are intentionally
   * absent: pp_btn has no L3/R3, and KEY 59/60 stay unmapped per the owner
   * (BRICKPRO_HANDOFF.md §10, 2026-10-19) so they remain free as custom
   * system buttons - PocketPlex must not consume or bind them.
   * Home: NO measured evdev code yet (the tg-0 spike never recorded one, and
   * the SDL-index plan in handoff §2 is moot under raw evdev). Stays
   * unmapped until the Phase 0 device pass checks whether Home emits
   * anything on event3 or another node; once a code is measured, the owner
   * decision (§10, 2026-10-18) is that Home quits straight to NextUI, the
   * same as the SP. */
};

pp_btn pp_tg5040_key_to_btn(int code) {
  for (size_t i = 0; i < sizeof pp_tg5040_keys / sizeof pp_tg5040_keys[0]; i++)
    if (pp_tg5040_keys[i].code == code) return swap_ab(pp_tg5040_keys[i].btn);
  return BTN_NONE;
}

pp_btn pp_tg5040_hat_to_btn(int axis, int value) {
  /* Sign convention is ASSUMED, not measured: the kernel hat convention is
   * -1 = left/up, +1 = right/down, and the tg-0 spike recorded the axes
   * (ABS 16/17) but not their signs. If the Phase 0 device pass shows the
   * Brick Pro inverts an axis, swap the two buttons on that axis HERE and
   * nowhere else, and record the measured signs in ops/LOG.md. Do not change
   * behaviour without an on-device measurement (review MINOR 2). */
  if (value == 0 || value < -1 || value > 1) return BTN_NONE;   /* hats: -1/0/+1 */
  if (axis == 16) return value < 0 ? BTN_LEFT : BTN_RIGHT;   /* ABS_HAT0X */
  if (axis == 17) return value < 0 ? BTN_UP : BTN_DOWN;     /* ABS_HAT0Y */
  return BTN_NONE;
}

pp_btn pp_tg5040_trigger_to_btn(int axis) {
  if (axis == 2) return BTN_L2;
  if (axis == 5) return BTN_R2;
  return BTN_NONE;
}
