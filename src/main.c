/* main.c: PocketPlex app entry point. Initializes platform + UI, then runs
 * the screen stack loop.
 *
 *   ./build/pocketplex                      full app (PIN link or dev fallback)
 *   ./build/pocketplex [--exit-after-ms N]  auto-quit after N ms
 *   ./build/pocketplex [--smoke-scroll]     auto-navigate all screens for perf testing
 *   ./build/pocketplex [--smoke-walk]       auto-navigate Library to Show to Season to Episode
 *   ./build/pocketplex --smoke-link         Link -> Servers -> Home (fake data if no token in ini)
 *   ./build/pocketplex --smoke-play <key> [--play-seconds N]
 *                                           play the first item at key for N s (default 30)
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
#include "ui/play_state.h"
#include "ui/session.h"
#include "ui/ui_stack.h"
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
  int link = 0;  /* --smoke-link: Link -> Servers -> Home (fake data when no token) */
  const char *play_key = NULL;  /* --smoke-play <key>: play the first item at key */
  char play_path[256];
  long play_s = 30;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--exit-after-ms") == 0 && i + 1 < argc)
      ui_set_exit_after_ms(atol(argv[++i]));
    else if (strcmp(argv[i], "--smoke-scroll") == 0)
      smoke = 1;
    else if (strcmp(argv[i], "--smoke-walk") == 0)
      walk = 1;
    else if (strcmp(argv[i], "--smoke-link") == 0)
      link = 1;
    else if (strcmp(argv[i], "--smoke-play") == 0 && i + 1 < argc)
      play_key = argv[++i];
    else if (strcmp(argv[i], "--play-seconds") == 0 && i + 1 < argc)
      play_s = atol(argv[++i]);
  }

  if (play_key) {
    if (play_normalize_key(play_key, play_path, sizeof play_path) != 0) {
      LOGE("--smoke-play: '%s' is not a ratingKey (digits) or a /library/... path", play_key);
      return 2;
    }
    play_key = play_path;
    if (play_s <= 0) { LOGE("--play-seconds must be > 0"); return 2; }
  }

  const char *ini = ini_path(argv[0]);
  pp_config cfg;
  pp_config_load(&cfg, ini);
  if (cfg.client_id[0] == '\0') pp_config_ensure_client_id(&cfg);

  if (pp_init(cfg.client_id) != PP_OK) {
    LOGE("pp_init failed");
    return 1;
  }

  if (smoke && !getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 0);
  int w = 0, h = 0;
  if (plat_init(&w, &h) != 0) {
    LOGE("plat_init failed");
    pp_cleanup();
    return 1;
  }

  ui_set_ini_path(ini);  /* before any screen reads the config */
  if (ui_init() != 0) {
    LOGE("ui_init failed");
    plat_quit();
    pp_cleanup();
    return 1;
  }

  pp_server srv;  /* ui_set_server deep-copies it */
  memset(&srv, 0, sizeof srv);

  if (smoke) {
    /* Fake provider only; starts at Link and walks the stack itself. */
    ui_set_smoke_scroll(1);
    ui_push_screen(SCREEN_LINK);
  } else if (play_key && !(cfg.token[0] && cfg.server_url[0])) {
    LOGE("--smoke-play needs server_url and token in the ini");
    ui_quit();
    pp_cleanup();
    plat_quit();
    return 2;
  } else if (play_key) {
    ui_set_smoke_play(play_key, play_s * 1000);
    srv.url = cfg.server_url;
    srv.token = (char *)pp_config_pms_token(&cfg);  /* const; ui_set_server deep-copies */
    srv.client_id = cfg.client_id;
    ui_set_server(&srv);
    ui_set_auth_token(cfg.token);
    ui_push(screen_list_create_key(play_key, "Play"));
  } else if (walk) {
    ui_set_smoke_walk(1);
    srv.url = cfg.server_url;
    srv.token = (char *)pp_config_pms_token(&cfg);  /* const; ui_set_server deep-copies */
    srv.client_id = cfg.client_id;
    ui_set_server(&srv);
    ui_set_auth_token(cfg.token);
    ui_push_screen(SCREEN_HOME);
  } else {
    if (link) {
      ui_set_smoke_link(1);
      if (!cfg.token[0]) ui_set_smoke_scroll(1);  /* fake PIN + fake servers */
    }
    /* No token -> Link; token only (fresh PIN link) -> Servers; token + url -> Home. */
    pp_screen_id start = session_start_screen(cfg.token, cfg.server_url);
    if (start != SCREEN_LINK) ui_set_auth_token(cfg.token);
    if (start == SCREEN_HOME) {
      srv.url = cfg.server_url;
      srv.token = (char *)pp_config_pms_token(&cfg);  /* const; ui_set_server deep-copies */
      srv.client_id = cfg.client_id;
      ui_set_server(&srv);
    }
    LOGI("start: %s (token %s, server_url %s)", ui_screen_name(start),
         cfg.token[0] ? "set" : "empty", cfg.server_url[0] ? "set" : "empty");
    ui_push_screen(start);
  }

  ui_set_swap_ab(cfg.swap_ab);
  memset(&cfg, 0, sizeof cfg);  /* the UI keeps its own copy of the token */

  LOGI("PocketPlex started: %dx%d (smoke=%d)", w, h, smoke);
  ui_run();

  /* A request still blocked in curl after ui_quit's wait would race
   * curl_global_cleanup, so skip pp_cleanup then (the process is exiting). */
  if (ui_quit() == 0) pp_cleanup();
  else LOGW("skipping pp_cleanup: requests still in flight");
  plat_quit();
  return ui_exit_code();
}
