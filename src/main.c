/* main.c: PocketPlex app entry point. Initializes platform + UI, then runs
 * the screen stack loop.
 *
 *   ./build/pocketplex                      full app (PIN link or dev fallback)
 *   ./build/pocketplex [--exit-after-ms N]  auto-quit after N ms
 *   ./build/pocketplex [--smoke-scroll]     auto-navigate all screens for perf testing
 *   ./build/pocketplex [--smoke-walk]       auto-navigate Library to Show to Season to Episode
 *   PP_FAKE_DELAY_MS=800 ./build/pocketplex --smoke-scroll
 *
 *   Esc or window close quits. --smoke-scroll auto-navigates all screens
 *   and logs fps to prove <50 ms frame times during async loading.
 *
 * Config: reads pocketplex.ini (POCKETPLEX_INI env overrides; else ./pocketplex.ini).
 * If [plex] server_url + token are set (dev fallback), Link/Servers screens are
 * skipped and the app goes straight to Home. Otherwise, PIN link flow.
 */
#include "platform/platform.h"
#include "ui/ui.h"
#include "plex/plex.h"
#include "config/config.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static long g_exit_after_ms = -1;

void ui_set_exit_after_ms(long ms) { g_exit_after_ms = ms; }

long  ui_get_exit_after_ms(void) { return g_exit_after_ms; }

static const char *ini_path(const char *argv0) {
  (void)argv0;
  const char *env = getenv("POCKETPLEX_INI");
  if (env && env[0]) return env;
  return "pocketplex.ini";
}

int main(int argc, char **argv) {
  int i;
  int smoke = 0;
  int walk = 0;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--exit-after-ms") == 0 && i + 1 < argc)
      ui_set_exit_after_ms(atol(argv[++i]));
    else if (strcmp(argv[i], "--smoke-scroll") == 0)
      smoke = 1;
    else if (strcmp(argv[i], "--smoke-walk") == 0)
      walk = 1;
  }

  const char *ini = ini_path(argv[0]);
  pp_config cfg;
  pp_config_load(&cfg, ini);
  if (!cfg.client_id[0]) pp_config_ensure_client_id(&cfg);

  if (pp_init(cfg.client_id) != PP_OK) {
    LOGE("pp_init failed");
    return 1;
  }

  if (!getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 0);
  int w = 0, h = 0;
  if (plat_init(&w, &h) != 0) {
    LOGE("plat_init failed");
    pp_cleanup();
    return 1;
  }

  if (ui_init() != 0) {
    LOGE("ui_init failed");
    plat_quit();
    pp_cleanup();
    return 1;
  }

  if (smoke) {
    ui_set_smoke_scroll(1);
    ui_push_screen(SCREEN_LINK);
  }

  pp_server srv;
  memset(&srv, 0, sizeof srv);

  if (walk) {
    ui_set_smoke_walk(1);
    srv.url = cfg.server_url;
    srv.token = cfg.token;
    srv.client_id = cfg.client_id;
    ui_set_server(&srv);
    ui_set_auth_token(cfg.token);
    ui_push_screen(SCREEN_HOME);
  } else if (cfg.token[0] && cfg.server_url[0]) {
    srv.url = cfg.server_url;
    srv.token = cfg.token;
    srv.client_id = cfg.client_id;
    ui_set_server(&srv);
    ui_set_auth_token(cfg.token);
    ui_push_screen(SCREEN_HOME);
  } else {
    ui_push_screen(SCREEN_LINK);
  }

  ui_set_swap_ab(cfg.swap_ab);
  ui_set_ini_path(ini);

  LOGI("PocketPlex started: %dx%d (smoke=%d)", w, h, smoke);
  ui_run();

  ui_quit();
  pp_cleanup();
  plat_quit();
  return 0;
}
