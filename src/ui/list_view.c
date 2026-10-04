/* list_view.c: pure UI logic for scrolling a list of items. No SDL dependency. */
#include "ui/ui.h"

void list_view_init(pp_list_view *lv, pp_list_model *model, int visible_count) {
  lv->model = model;
  lv->selected = 0;
  lv->scroll_top = 0;
  lv->visible_count = visible_count > 0 ? visible_count : 1;
}

void list_view_ensure_visible(pp_list_view *lv) {
  if (lv->selected < lv->scroll_top) lv->scroll_top = lv->selected;
  if (lv->selected >= lv->scroll_top + lv->visible_count)
    lv->scroll_top = lv->selected - lv->visible_count + 1;
  if (lv->scroll_top < 0) lv->scroll_top = 0;
  if (lv->model && lv->scroll_top > lv->model->count - lv->visible_count) {
    int max_top = lv->model->count - lv->visible_count;
    if (max_top < 0) max_top = 0;
    lv->scroll_top = max_top;
  }
}

void list_view_handle_button(pp_list_view *lv, pp_btn btn) {
  int total = lv->model ? lv->model->count : 0;
  if (total == 0) return;

  switch (btn) {
  case BTN_DOWN:
    if (lv->selected < total - 1) lv->selected++;
    list_view_ensure_visible(lv);
    break;
  case BTN_UP:
    if (lv->selected > 0) lv->selected--;
    list_view_ensure_visible(lv);
    break;
  case BTN_R1: /* page down */
    list_view_page_down(lv);
    break;
  case BTN_L1: /* page up */
    list_view_page_up(lv);
    break;
  case BTN_A:
  case BTN_B:
  case BTN_LEFT:
  case BTN_RIGHT:
  case BTN_X:
  case BTN_Y:
  case BTN_START:
  case BTN_SELECT:
  case BTN_MENU:
  case BTN_L2:
  case BTN_R2:
  case BTN_NONE:
    break;
  }
}

int list_view_page_up(pp_list_view *lv) {
  int step = lv->visible_count - 1;
  int new_sel = lv->selected - step;
  if (new_sel < 0) new_sel = 0;
  lv->selected = new_sel;
  lv->scroll_top = new_sel;
  return lv->selected;
}

int list_view_page_down(pp_list_view *lv) {
  int step = lv->visible_count - 1;
  int total = lv->model ? lv->model->count : 0;
  int new_sel = lv->selected + step;
  if (new_sel >= total) new_sel = total - 1;
  if (new_sel < 0) new_sel = 0;
  lv->selected = new_sel;
  lv->scroll_top = new_sel;
  if (lv->model && lv->scroll_top > lv->model->count - lv->visible_count) {
    int max_top = lv->model->count - lv->visible_count;
    if (max_top < 0) max_top = 0;
    lv->scroll_top = max_top;
  }
  return lv->selected;
}
