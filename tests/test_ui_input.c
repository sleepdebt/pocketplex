#include "test.h"
#include "ui/ui.h"

static void test_arrow_keys_map_to_dpad(void) {
  CHECK(pp_keycode_to_btn(PP_SDLK_DOWN) == BTN_DOWN);
  CHECK(pp_keycode_to_btn(PP_SDLK_UP) == BTN_UP);
  CHECK(pp_keycode_to_btn(PP_SDLK_LEFT) == BTN_LEFT);
  CHECK(pp_keycode_to_btn(PP_SDLK_RIGHT) == BTN_RIGHT);
}

static void test_enter_is_a(void) {
  CHECK(pp_keycode_to_btn(PP_SDLK_RETURN) == BTN_A);
}

static void test_backspace_is_b(void) {
  CHECK(pp_keycode_to_btn(PP_SDLK_BACKSPACE) == BTN_B);
}

static void test_page_keys_are_l1_r1(void) {
  CHECK(pp_keycode_to_btn(PP_SDLK_PAGEUP) == BTN_L1);
  CHECK(pp_keycode_to_btn(PP_SDLK_PAGEDOWN) == BTN_R1);
}

static void test_escape_is_menu(void) {
  CHECK(pp_keycode_to_btn(PP_SDLK_ESCAPE) == BTN_MENU);
}

static void test_unknown_key_is_none(void) {
  CHECK(pp_keycode_to_btn('q') == BTN_NONE);
  CHECK(pp_keycode_to_btn(0) == BTN_NONE);
}

static void test_default_keys_table_terminates(void) {
  int i;
  for (i = 0; i < 64; i++) {
    if (pp_default_keys[i].btn == BTN_NONE) break;
  }
  CHECK(i < 64);
  CHECK(pp_default_keys[i].sdl_key == 0);
}

static void test_swap_ab_swaps_enter_and_backspace(void) {
  ui_set_swap_ab(0);
  CHECK(pp_keycode_to_btn(PP_SDLK_RETURN) == BTN_A);
  CHECK(pp_keycode_to_btn(PP_SDLK_BACKSPACE) == BTN_B);
  ui_set_swap_ab(1);
  CHECK(pp_keycode_to_btn(PP_SDLK_RETURN) == BTN_B);
  CHECK(pp_keycode_to_btn(PP_SDLK_BACKSPACE) == BTN_A);
  ui_set_swap_ab(0);
}

static void test_swap_ab_does_not_affect_other_buttons(void) {
  ui_set_swap_ab(1);
  CHECK(pp_keycode_to_btn(PP_SDLK_DOWN) == BTN_DOWN);
  CHECK(pp_keycode_to_btn(PP_SDLK_ESCAPE) == BTN_MENU);
  ui_set_swap_ab(0);
}

/* Gamepad (SDL GameController button indices, as PP_PAD_*). */
static void test_pad_mapping(void) {
  ui_set_swap_ab(0);
  CHECK(pp_pad_button_to_btn(PP_PAD_A) == BTN_A);
  CHECK(pp_pad_button_to_btn(PP_PAD_B) == BTN_B);
  CHECK(pp_pad_button_to_btn(PP_PAD_X) == BTN_X);
  CHECK(pp_pad_button_to_btn(PP_PAD_Y) == BTN_Y);
  CHECK(pp_pad_button_to_btn(PP_PAD_BACK) == BTN_SELECT);
  CHECK(pp_pad_button_to_btn(PP_PAD_START) == BTN_START);
  CHECK(pp_pad_button_to_btn(PP_PAD_GUIDE) == BTN_MENU);
  CHECK(pp_pad_button_to_btn(PP_PAD_LEFTSHOULDER) == BTN_L1);
  CHECK(pp_pad_button_to_btn(PP_PAD_RIGHTSHOULDER) == BTN_R1);
  CHECK(pp_pad_button_to_btn(PP_PAD_DPAD_UP) == BTN_UP);
  CHECK(pp_pad_button_to_btn(PP_PAD_DPAD_RIGHT) == BTN_RIGHT);
}

/* swap_ab must apply to the gamepad too (it only applied to the keyboard). */
static void test_pad_swap_ab(void) {
  ui_set_swap_ab(1);
  CHECK(pp_pad_button_to_btn(PP_PAD_A) == BTN_B);
  CHECK(pp_pad_button_to_btn(PP_PAD_B) == BTN_A);
  CHECK(pp_pad_button_to_btn(PP_PAD_X) == BTN_X);
  ui_set_swap_ab(0);
}

/* Only an explicit Menu input can quit: unmapped pad buttons/keys are BTN_NONE. */
static void test_unmapped_cannot_quit(void) {
  CHECK(pp_pad_button_to_btn(PP_PAD_LEFTSTICK) == BTN_NONE);
  CHECK(pp_pad_button_to_btn(PP_PAD_RIGHTSTICK) == BTN_NONE);
  CHECK(pp_pad_button_to_btn(-1) == BTN_NONE);
  CHECK(pp_pad_button_to_btn(15) == BTN_NONE);     /* SDL misc/paddles */
  CHECK(pp_pad_button_to_btn(1000) == BTN_NONE);
  int menu_pad = 0, menu_keys = 0;
  for (int b = -1; b < 64; b++) if (pp_pad_button_to_btn(b) == BTN_MENU) menu_pad++;
  for (int i = 0; pp_default_keys[i].btn != BTN_NONE; i++)
    if (pp_default_keys[i].btn == BTN_MENU) menu_keys++;
  CHECK(menu_pad == 1);                             /* GUIDE only */
  CHECK(menu_keys == 1);                            /* Escape only */
  CHECK(pp_keycode_to_btn('q') == BTN_NONE);
}

static void test_btn_names(void) {
  CHECK_STR(pp_btn_name(BTN_A), "A");
  CHECK_STR(pp_btn_name(BTN_MENU), "MENU");
  CHECK_STR(pp_btn_name(BTN_NONE), "none");
}

/* SP device 2026-10-05: 'Anbernic RG35XX-SP Controller' is mapped by position
 * (Xbox): physical A (east) arrives as SDL B. Nintendo layout = labels win. */
static void test_layout_detection(void) {
  CHECK(pp_pad_layout_for("Anbernic RG35XX-SP Controller", 0, 0) == PP_LAYOUT_NINTENDO);
  CHECK(pp_pad_layout_for("Anbernic RG35XX-SP Controller", 0, 1) == PP_LAYOUT_NINTENDO);
  CHECK(pp_pad_layout_for("RG40XX-H gamepad", 0, 0) == PP_LAYOUT_NINTENDO);
  CHECK(pp_pad_layout_for("Miyoo Mini Plus", 0, 0) == PP_LAYOUT_NINTENDO);
  CHECK(pp_pad_layout_for("Xbox Wireless Controller", 0, 0) == PP_LAYOUT_POSITIONAL);
  CHECK(pp_pad_layout_for("Xbox Wireless Controller", 0, 1) == PP_LAYOUT_POSITIONAL);  /* Xbox pad on the SP */
  CHECK(pp_pad_layout_for("PS5 DualSense", 0, 1) == PP_LAYOUT_POSITIONAL);
  /* SDL already maps Switch controllers by label (USE_BUTTON_LABELS): don't swap twice. */
  CHECK(pp_pad_layout_for("Nintendo Switch Pro Controller", 1, 0) == PP_LAYOUT_POSITIONAL);
  CHECK(pp_pad_layout_for("Generic USB gamepad", 0, 0) == PP_LAYOUT_POSITIONAL);  /* desktop default */
  CHECK(pp_pad_layout_for("Generic USB gamepad", 0, 1) == PP_LAYOUT_NINTENDO);    /* sp/mmp default */
  CHECK(pp_pad_layout_for(NULL, 0, 1) == PP_LAYOUT_NINTENDO);
  CHECK(pp_pad_layout_for(NULL, 0, 0) == PP_LAYOUT_POSITIONAL);
  CHECK_STR(pp_pad_layout_name(PP_LAYOUT_NINTENDO), "nintendo");
  CHECK_STR(pp_pad_layout_name(PP_LAYOUT_POSITIONAL), "positional");
}

/* Both layouts: what each SDL face button means. */
static void test_mapping_both_layouts(void) {
  ui_set_swap_ab(0);
  pp_pad_set_layout(PP_LAYOUT_POSITIONAL);
  CHECK(pp_pad_button_to_btn(PP_PAD_A) == BTN_A);
  CHECK(pp_pad_button_to_btn(PP_PAD_B) == BTN_B);
  CHECK(pp_pad_button_to_btn(PP_PAD_X) == BTN_X);
  CHECK(pp_pad_button_to_btn(PP_PAD_Y) == BTN_Y);

  pp_pad_set_layout(PP_LAYOUT_NINTENDO);          /* the SP */
  CHECK(pp_pad_button_to_btn(PP_PAD_B) == BTN_A);  /* physical A (east) -> A: the device bug */
  CHECK(pp_pad_button_to_btn(PP_PAD_A) == BTN_B);  /* physical B (south) */
  CHECK(pp_pad_button_to_btn(PP_PAD_Y) == BTN_X);  /* physical X (north) */
  CHECK(pp_pad_button_to_btn(PP_PAD_X) == BTN_Y);  /* physical Y (west) */
  CHECK(pp_pad_button_to_btn(PP_PAD_DPAD_UP) == BTN_UP);       /* others untouched */
  CHECK(pp_pad_button_to_btn(PP_PAD_START) == BTN_START);
  CHECK(pp_pad_button_to_btn(PP_PAD_GUIDE) == BTN_MENU);

  /* [ui] swap_ab / the Settings toggle overrides on top of the layout. */
  ui_set_swap_ab(1);
  CHECK(pp_pad_button_to_btn(PP_PAD_B) == BTN_B);
  CHECK(pp_pad_button_to_btn(PP_PAD_A) == BTN_A);
  CHECK(pp_pad_button_to_btn(PP_PAD_Y) == BTN_X);  /* swap_ab leaves X/Y alone */
  ui_set_swap_ab(0);

  /* The keyboard never follows the pad layout. */
  CHECK(pp_keycode_to_btn(PP_SDLK_RETURN) == BTN_A);
  pp_pad_set_layout(PP_LAYOUT_POSITIONAL);
}

/* ---- Display layout (scale = h/480) --------------------------------------- */

static void test_layout_default_is_640x480(void) {
  pp_layout_set_display(0, 0);   /* reset to the design size */
  CHECK(pp_layout_w() == 640);
  CHECK(pp_layout_h() == 480);
  CHECK(pp_layout_scale() > 0.999 && pp_layout_scale() < 1.001);
  CHECK(pp_layout_px(20) == 20);
  CHECK(pp_layout_font_pt() == 20);
  CHECK(pp_layout_font_bold_pt() == 22);
  CHECK(pp_layout_line_h() == 26);
  CHECK(pp_layout_header_h() == 40);
  CHECK(pp_layout_footer_h() == 32);
  CHECK(pp_layout_margin_l() == 16);
  CHECK(pp_layout_margin_r() == 16);
  CHECK(pp_layout_visible() == 15);   /* (480-40-32)/26 */
}

/* The Brick Pro panel: 1024x768 = 1.6x the design height. */
static void test_layout_scales_for_1024x768(void) {
  pp_layout_set_display(1024, 768);
  CHECK(pp_layout_w() == 1024);
  CHECK(pp_layout_h() == 768);
  CHECK(pp_layout_scale() > 1.599 && pp_layout_scale() < 1.601);
  CHECK(pp_layout_font_pt() == 32);        /* 20 * 1.6 */
  CHECK(pp_layout_font_bold_pt() == 35);   /* 22 * 1.6 = 35.2 */
  CHECK(pp_layout_line_h() == 42);         /* 26 * 1.6 = 41.6 */
  CHECK(pp_layout_header_h() == 64);       /* 40 * 1.6 */
  CHECK(pp_layout_footer_h() == 51);       /* 32 * 1.6 = 51.2 */
  CHECK(pp_layout_margin_l() == 26);       /* 16 * 1.6 = 25.6 */
  CHECK(pp_layout_px(24) == 38);           /* 38.4 */
  CHECK(pp_layout_visible() == 15);        /* (768-64-51)/42 */
  pp_layout_set_display(640, 480);         /* back to the design size */
  CHECK(pp_layout_font_pt() == 20);
  CHECK(pp_layout_px(24) == 24);
  pp_layout_set_display(-1, -1);           /* invalid resets too */
  CHECK(pp_layout_w() == 640 && pp_layout_h() == 480);
}

/* PP_FAKE_DISPLAY=WxH parsing (desktop layout checks). */
static void test_parse_display_size(void) {
  int w = 0, h = 0;
  CHECK(pp_parse_display_size("1024x768", &w, &h) == 0);
  CHECK(w == 1024 && h == 768);
  CHECK(pp_parse_display_size("640x480", &w, &h) == 0);
  CHECK(w == 640 && h == 480);
  CHECK(pp_parse_display_size(NULL, &w, &h) == -1);
  CHECK(pp_parse_display_size("", &w, &h) == -1);
  CHECK(pp_parse_display_size("abc", &w, &h) == -1);
  CHECK(pp_parse_display_size("1024", &w, &h) == -1);
  CHECK(pp_parse_display_size("1024x768junk", &w, &h) == -1);
  CHECK(pp_parse_display_size("100x100", &w, &h) == -1);      /* below minimum */
  CHECK(pp_parse_display_size("9999x9999", &w, &h) == -1);    /* above maximum */
  CHECK(pp_parse_display_size("0x0", &w, &h) == -1);
  CHECK(pp_parse_display_size("-640x480", &w, &h) == -1);
}

int main(void) {
  RUN(test_arrow_keys_map_to_dpad);
  RUN(test_enter_is_a);
  RUN(test_backspace_is_b);
  RUN(test_page_keys_are_l1_r1);
  RUN(test_escape_is_menu);
  RUN(test_swap_ab_swaps_enter_and_backspace);
  RUN(test_swap_ab_does_not_affect_other_buttons);
  RUN(test_unknown_key_is_none);
  RUN(test_default_keys_table_terminates);
  RUN(test_pad_mapping);
  RUN(test_pad_swap_ab);
  RUN(test_unmapped_cannot_quit);
  RUN(test_btn_names);
  RUN(test_layout_detection);
  RUN(test_mapping_both_layouts);
  RUN(test_layout_default_is_640x480);
  RUN(test_layout_scales_for_1024x768);
  RUN(test_parse_display_size);
  return TEST_RESULT();
}
