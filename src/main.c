/* main.c: PocketPlex app entry point. Initializes platform + UI, then runs
 * the screen stack loop.
 *
 *   ./build/pocketplex [--exit-after-ms N]
 *   ./build/pocketplex [--smoke-scroll]
 *   PP_FAKE_DELAY_MS=800 ./build/pocketplex --smoke-scroll
 *
 *   Esc or window close quits. --smoke-scroll auto-navigates all screens
 *   and logs fps to prove <50 ms frame times during async loading.
 */
#include "platform/platform.h"
#include "ui/ui.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static long g_exit_after_ms = -1;

void ui_set_exit_after_ms(long ms) { g_exit_after_ms = ms; }
long  ui_get_exit_after_ms(void) { return g_exit_after_ms; }

int main(int argc, char **argv) {
  int i;
  int smoke = 0;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--exit-after-ms") == 0 && i + 1 < argc)
      ui_set_exit_after_ms(atol(argv[++i]));
    else if (strcmp(argv[i], "--smoke-scroll") == 0)
      smoke = 1;
  }

  int w = 0, h = 0;
  if (plat_init(&w, &h) != 0) {
    LOGE("plat_init failed");
    return 1;
  }

  if (ui_init() != 0) {
    LOGE("ui_init failed");
    plat_quit();
    return 1;
  }

  if (smoke) ui_set_smoke_scroll(1);

  LOGI("PocketPlex started: %dx%d (smoke=%d)", w, h, smoke);
  ui_run();

  ui_quit();
  plat_quit();
  return 0;
}
