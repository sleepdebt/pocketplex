/* ui_stack.c: the screen stack. See ui_stack.h. */
#include "ui/ui_stack.h"
#include "log.h"

#include <stdlib.h>

static pp_screen *g_stack[UI_STACK_MAX];
static int g_depth = 0;
/* Removed this frame, destroyed at finalize. A frame can at most remove the
 * whole stack plus what it pushed, so twice the stack size is plenty. */
static pp_screen *g_dead[2 * UI_STACK_MAX];
static int g_dead_count = 0;

static const char *const k_names[SCREEN_COUNT] = {
  "Link", "Servers", "Home", "List", "Detail", "Settings", "Player", "Toast",
};

const char *ui_screen_name(pp_screen_id id) {
  return ((int)id >= 0 && id < SCREEN_COUNT) ? k_names[id] : "?";
}

static void destroy_now(pp_screen *s) {
  if (s->destroy) s->destroy(s);
  free(s);
}

static void retire(pp_screen *s) {
  if (g_dead_count < (int)(sizeof g_dead / sizeof g_dead[0])) {
    g_dead[g_dead_count++] = s;
  } else {
    LOGE("ui: retire queue full, destroying %s now", ui_screen_name(s->id));
    destroy_now(s);
  }
}

static pp_screen *remove_top(void) {
  pp_screen *s = g_stack[--g_depth];
  g_stack[g_depth] = NULL;
  retire(s);
  return s;
}

int ui_stack_push(pp_screen *s) {
  if (!s) return -1;
  if (g_depth >= UI_STACK_MAX) {
    LOGE("ui: stack full, dropping %s", ui_screen_name(s->id));
    destroy_now(s);
    return -1;
  }
  g_stack[g_depth++] = s;
  LOGI("ui: push %s (depth %d)", ui_screen_name(s->id), g_depth);
  return 0;
}

int ui_stack_pop(void) {
  if (g_depth <= 1) {
    if (g_depth == 1)
      LOGI("ui: pop ignored on root %s (Menu quits)", ui_screen_name(g_stack[0]->id));
    return -1;
  }
  pp_screen *s = remove_top();
  LOGI("ui: pop %s (depth %d)", ui_screen_name(s->id), g_depth);
  return 0;
}

int ui_stack_replace(pp_screen *s) {
  if (!s) return -1;
  if (g_depth == 0) return ui_stack_push(s);
  pp_screen *old = remove_top();
  g_stack[g_depth++] = s;
  LOGI("ui: replace %s -> %s (depth %d)", ui_screen_name(old->id), ui_screen_name(s->id), g_depth);
  return 0;
}

int ui_stack_reset(pp_screen *s) {
  if (!s) return -1;
  while (g_depth > 0) remove_top();
  g_stack[g_depth++] = s;
  LOGI("ui: reset to %s (depth 1)", ui_screen_name(s->id));
  return 0;
}

void ui_stack_pop_all(void) {
  if (g_depth) LOGI("ui: pop all (%d)", g_depth);
  while (g_depth > 0) remove_top();
}

void ui_stack_finalize(void) {
  for (int i = 0; i < g_dead_count; i++) destroy_now(g_dead[i]);
  g_dead_count = 0;
}

ui_nav_op ui_nav_for(pp_screen_id target) {
  return (target == SCREEN_LINK || target == SCREEN_SERVERS || target == SCREEN_HOME)
         ? UI_NAV_RESET : UI_NAV_PUSH;
}

int ui_stack_go(pp_screen *s) {
  if (!s) return -1;
  return ui_nav_for(s->id) == UI_NAV_RESET ? ui_stack_reset(s) : ui_stack_push(s);
}

pp_screen *ui_stack_top(void) { return g_depth > 0 ? g_stack[g_depth - 1] : NULL; }
int ui_stack_depth(void) { return g_depth; }
