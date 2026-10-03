#include "test.h"
#include "ui/ui.h"

static void test_arrow_keys_map_to_dpad(void) {
  CHECK(pp_keycode_to_btn(0x28) == BTN_DOWN);   /* SDLK_DOWN */
  CHECK(pp_keycode_to_btn(0x26) == BTN_UP);      /* SDLK_UP */
  CHECK(pp_keycode_to_btn(0x25) == BTN_LEFT);    /* SDLK_LEFT */
  CHECK(pp_keycode_to_btn(0x27) == BTN_RIGHT);   /* SDLK_RIGHT */
}

static void test_enter_is_a(void) {
  CHECK(pp_keycode_to_btn(0x0D) == BTN_A);       /* SDLK_RETURN */
}

static void test_backspace_is_b(void) {
  CHECK(pp_keycode_to_btn(0x08) == BTN_B);       /* SDLK_BACKSPACE */
}

static void test_page_keys_are_l1_r1(void) {
  CHECK(pp_keycode_to_btn(0x21) == BTN_L1);      /* SDLK_PAGEUP */
  CHECK(pp_keycode_to_btn(0x22) == BTN_R1);      /* SDLK_PAGEDOWN */
}

static void test_escape_is_menu(void) {
  CHECK(pp_keycode_to_btn(0x1B) == BTN_MENU);    /* SDLK_ESCAPE */
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

int main(void) {
  RUN(test_arrow_keys_map_to_dpad);
  RUN(test_enter_is_a);
  RUN(test_backspace_is_b);
  RUN(test_page_keys_are_l1_r1);
  RUN(test_escape_is_menu);
  RUN(test_unknown_key_is_none);
  RUN(test_default_keys_table_terminates);
  return TEST_RESULT();
}
