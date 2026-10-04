#include <stdlib.h>
#include "test.h"
#include "ui/ui.h"

static pp_item *make_items(int n) {
  pp_item *items = (pp_item *)malloc(n * sizeof(pp_item));
  int i;
  for (i = 0; i < n; i++) {
    items[i].kind = PP_EPISODE;
    items[i].title[0] = 'E';
    items[i].title[1] = 'p';
    items[i].title[2] = 'i';
    items[i].title[3] = (char)('0' + (i % 10));
    items[i].title[4] = 0;
    items[i].duration_ms = 1200000;
    items[i].view_offset_ms = 0;
    items[i].watched = 0;
  }
  return items;
}

static void test_init_defaults(void) {
  pp_item items[10];
  pp_list_model model = { items, 10, 10 };
  pp_list_view lv;
  list_view_init(&lv, &model, 5);
  CHECK(lv.selected == 0);
  CHECK(lv.scroll_top == 0);
  CHECK(lv.visible_count == 5);
}

static void test_down_moves_selection(void) {
  pp_item *items = make_items(10);
  pp_list_model model = { items, 10, 10 };
  pp_list_view lv;
  list_view_init(&lv, &model, 5);
  list_view_handle_button(&lv, BTN_DOWN);
  CHECK(lv.selected == 1);
  list_view_handle_button(&lv, BTN_DOWN);
  CHECK(lv.selected == 2);
  free(items);
}

static void test_up_clamps_at_zero(void) {
  pp_item *items = make_items(10);
  pp_list_model model = { items, 10, 10 };
  pp_list_view lv;
  list_view_init(&lv, &model, 5);
  list_view_handle_button(&lv, BTN_UP);
  CHECK(lv.selected == 0);
  free(items);
}

static void test_scroll_on_down_past_visible(void) {
  pp_item *items = make_items(20);
  pp_list_model model = { items, 20, 20 };
  pp_list_view lv;
  /* visible = 5, so after 5 downs, scroll_top should advance */
  list_view_init(&lv, &model, 5);
  int i;
  for (i = 0; i < 5; i++) list_view_handle_button(&lv, BTN_DOWN);
  CHECK(lv.selected == 5);
  CHECK(lv.scroll_top == 1);
  for (i = 0; i < 4; i++) list_view_handle_button(&lv, BTN_DOWN);
  CHECK(lv.selected == 9);
  CHECK(lv.scroll_top == 5);
  free(items);
}

static void test_page_down(void) {
  pp_item *items = make_items(100);
  pp_list_model model = { items, 100, 100 };
  pp_list_view lv;
  list_view_init(&lv, &model, 10);
  CHECK(list_view_page_down(&lv) == 9);
  CHECK(lv.scroll_top == 9);
  CHECK(lv.selected == 9);
  CHECK(list_view_page_down(&lv) == 18);
  free(items);
}

static void test_page_up(void) {
  pp_item *items = make_items(100);
  pp_list_model model = { items, 100, 100 };
  pp_list_view lv;
  list_view_init(&lv, &model, 10);
  lv.selected = 50;
  lv.scroll_top = 41;
  CHECK(list_view_page_up(&lv) == 41);
  CHECK(lv.selected == 41);
  CHECK(lv.scroll_top == 41);
}

static void test_page_down_clamps_at_end(void) {
  pp_item *items = make_items(13);
  pp_list_model model = { items, 13, 13 };
  pp_list_view lv;
  list_view_init(&lv, &model, 10);
  list_view_page_down(&lv); /* sel 9 */
  CHECK(list_view_page_down(&lv) == 12); /* clamped to 12 */
  CHECK(lv.scroll_top == 3); /* 13 - 10 = 3 */
  free(items);
}

static void test_2000_item_scroll_performance(void) {
  pp_item *items = make_items(2000);
  pp_list_model model = { items, 2000, 2000 };
  pp_list_view lv;
  list_view_init(&lv, &model, 15);
  int i;
  for (i = 0; i < 1999; i++) list_view_handle_button(&lv, BTN_DOWN);
  CHECK(lv.selected == 1999);
  CHECK(lv.scroll_top == 1999 - 15 + 1);
  free(items);
}

int main(void) {
  RUN(test_init_defaults);
  RUN(test_down_moves_selection);
  RUN(test_up_clamps_at_zero);
  RUN(test_scroll_on_down_past_visible);
  RUN(test_page_down);
  RUN(test_page_up);
  RUN(test_page_down_clamps_at_end);
  RUN(test_2000_item_scroll_performance);
  return TEST_RESULT();
}
