/* tests/test_ui_session.c: start-screen decision and server-choice persistence.
 * Device bug 2026-10-05: a token-only ini (normal after a PIN link) went back to Link. */
#include <string.h>
#include "test.h"
#include "ui/session.h"

static void test_start_screen(void) {
  CHECK(session_start_screen("", "") == SCREEN_LINK);
  CHECK(session_start_screen(NULL, NULL) == SCREEN_LINK);
  CHECK(session_start_screen("", "http://pms:32400") == SCREEN_LINK);       /* no token: relink */
  CHECK(session_start_screen("tok", "") == SCREEN_SERVERS);                 /* after PIN link */
  CHECK(session_start_screen("tok", NULL) == SCREEN_SERVERS);
  CHECK(session_start_screen("tok", "http://pms:32400") == SCREEN_HOME);
}

static void test_apply_server_choice(void) {
  pp_config cfg;
  pp_config_defaults(&cfg);
  snprintf(cfg.token, sizeof cfg.token, "account-REDACTED");
  pp_server srv = { "https://10-0-0-2.abc.plex.direct:32400", "server-REDACTED", "cid" };
  CHECK(session_apply_server(&cfg, &srv) == 0);
  CHECK_STR(cfg.server_url, "https://10-0-0-2.abc.plex.direct:32400");
  CHECK_STR(cfg.token, "server-REDACTED");          /* resource accessToken wins */
  CHECK(session_start_screen(cfg.token, cfg.server_url) == SCREEN_HOME);

  pp_server no_tok = { "http://192.168.1.10:32400", NULL, NULL };
  CHECK(session_apply_server(&cfg, &no_tok) == 0);
  CHECK_STR(cfg.server_url, "http://192.168.1.10:32400");
  CHECK_STR(cfg.token, "server-REDACTED");          /* kept when the resource has none */
  pp_server empty_tok = { "http://h", "", NULL };
  CHECK(session_apply_server(&cfg, &empty_tok) == 0);
  CHECK_STR(cfg.token, "server-REDACTED");

  pp_server no_url = { NULL, "x", NULL };
  CHECK(session_apply_server(&cfg, &no_url) < 0);   /* nothing to persist */
  CHECK(session_apply_server(&cfg, NULL) < 0);
  CHECK(session_apply_server(NULL, &srv) < 0);
  char longurl[600];
  memset(longurl, 'a', sizeof longurl - 1);
  longurl[sizeof longurl - 1] = 0;
  pp_server too_long = { longurl, NULL, NULL };
  CHECK(session_apply_server(&cfg, &too_long) < 0);  /* would truncate */
  CHECK_STR(cfg.server_url, "http://h");            /* unchanged on failure */
}

int main(void) {
  RUN(test_start_screen);
  RUN(test_apply_server_choice);
  return TEST_RESULT();
}
