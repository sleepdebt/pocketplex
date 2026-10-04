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
  return TEST_RESULT();
}
