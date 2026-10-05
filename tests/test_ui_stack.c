/* tests/test_ui_stack.c: screen stack semantics. No SDL.
 * Device bug 2026-10-05: "crashed back to Ports when choosing the server"
 * right after PIN link. These pin down pop/push/replace in one frame. */
#include <stdlib.h>
#include "test.h"
#include "ui/ui_stack.h"

static int g_destroyed[SCREEN_COUNT];

static void count_destroy(pp_screen *s) { g_destroyed[s->id]++; }

static pp_screen *mk(pp_screen_id id) {
  pp_screen *s = calloc(1, sizeof(*s));
  s->id = id;
  s->destroy = count_destroy;
  return s;
}

static void reset_all(void) {
  ui_stack_pop_all();
  ui_stack_finalize();
  memset(g_destroyed, 0, sizeof g_destroyed);
}

/* Lead's hypothesis: a deferred pop removes the newly pushed Servers. */
static void test_pop_then_push_same_frame_keeps_new_top(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_LINK));
  ui_stack_push(mk(SCREEN_HOME));        /* non-root, so the pop is honoured */
  ui_stack_pop();
  ui_stack_push(mk(SCREEN_SERVERS));
  CHECK(ui_stack_top()->id == SCREEN_SERVERS);
  CHECK(ui_stack_depth() == 2);
  ui_stack_finalize();                   /* end of frame */
  CHECK(ui_stack_top()->id == SCREEN_SERVERS);
  CHECK(g_destroyed[SCREEN_HOME] == 1);
  CHECK(g_destroyed[SCREEN_SERVERS] == 0);
}

/* Link -> Servers after PIN confirm: Link is the only screen. */
static void test_replace_root(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_LINK));
  ui_stack_replace(mk(SCREEN_SERVERS));
  CHECK(ui_stack_depth() == 1);
  CHECK(ui_stack_top()->id == SCREEN_SERVERS);
  ui_stack_finalize();
  CHECK(ui_stack_depth() == 1);
  CHECK(ui_stack_top()->id == SCREEN_SERVERS);
  CHECK(g_destroyed[SCREEN_LINK] == 1);
  CHECK(g_destroyed[SCREEN_SERVERS] == 0);
}

/* B on the only screen must not empty the stack (= quit the app). */
static void test_pop_root_is_refused(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_SERVERS));
  CHECK(ui_stack_pop() < 0);
  ui_stack_finalize();
  CHECK(ui_stack_depth() == 1);
  CHECK(ui_stack_top()->id == SCREEN_SERVERS);
  CHECK(g_destroyed[SCREEN_SERVERS] == 0);
}

static void test_pop_non_root(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_HOME));
  ui_stack_push(mk(SCREEN_SETTINGS));
  CHECK(ui_stack_pop() == 0);
  CHECK(ui_stack_top()->id == SCREEN_HOME);
  ui_stack_finalize();
  CHECK(g_destroyed[SCREEN_SETTINGS] == 1);
}

/* Sign-out: everything goes, Link is the only screen. */
static void test_reset(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_HOME));
  ui_stack_push(mk(SCREEN_LIST));
  ui_stack_push(mk(SCREEN_SETTINGS));
  ui_stack_reset(mk(SCREEN_LINK));
  CHECK(ui_stack_depth() == 1);
  CHECK(ui_stack_top()->id == SCREEN_LINK);
  ui_stack_finalize();
  CHECK(g_destroyed[SCREEN_HOME] == 1 && g_destroyed[SCREEN_LIST] == 1 &&
        g_destroyed[SCREEN_SETTINGS] == 1 && g_destroyed[SCREEN_LINK] == 0);
}

/* Handlers read their own data after replacing themselves: freed only at finalize. */
static void test_replaced_screen_lives_until_finalize(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_LIST));
  ui_stack_replace(mk(SCREEN_DETAIL));
  CHECK(g_destroyed[SCREEN_LIST] == 0);
  ui_stack_finalize();
  CHECK(g_destroyed[SCREEN_LIST] == 1);
}

static void test_cap_frees_overflow(void) {
  reset_all();
  for (int i = 0; i < UI_STACK_MAX; i++) CHECK(ui_stack_push(mk(SCREEN_LIST)) == 0);
  CHECK(ui_stack_push(mk(SCREEN_DETAIL)) < 0);
  CHECK(g_destroyed[SCREEN_DETAIL] == 1);   /* not leaked */
  CHECK(ui_stack_depth() == UI_STACK_MAX);
  ui_stack_push(NULL);
  ui_stack_replace(NULL);
  CHECK(ui_stack_depth() == UI_STACK_MAX);
}

/* Shutdown path empties everything, root included. */
static void test_pop_all(void) {
  reset_all();
  ui_stack_push(mk(SCREEN_HOME));
  ui_stack_push(mk(SCREEN_LIST));
  ui_stack_pop_all();
  CHECK(ui_stack_depth() == 0);
  CHECK(ui_stack_top() == NULL);
  ui_stack_finalize();
  CHECK(g_destroyed[SCREEN_HOME] == 1 && g_destroyed[SCREEN_LIST] == 1);
}

static void test_names(void) {
  CHECK_STR(ui_screen_name(SCREEN_SERVERS), "Servers");
  CHECK_STR(ui_screen_name(SCREEN_LINK), "Link");
  CHECK_STR(ui_screen_name((pp_screen_id)99), "?");
}

int main(void) {
  RUN(test_pop_then_push_same_frame_keeps_new_top);
  RUN(test_replace_root);
  RUN(test_pop_root_is_refused);
  RUN(test_pop_non_root);
  RUN(test_reset);
  RUN(test_replaced_screen_lives_until_finalize);
  RUN(test_cap_frees_overflow);
  RUN(test_pop_all);
  RUN(test_names);
  reset_all();
  return TEST_RESULT();
}
