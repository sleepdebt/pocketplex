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
  pp_server srv = { "https://10-0-0-2.abc.plex.direct:32400", "server-REDACTED", "cid", NULL };
  CHECK(session_apply_server(&cfg, &srv) == 0);
  CHECK_STR(cfg.server_url, "https://10-0-0-2.abc.plex.direct:32400");
  CHECK_STR(cfg.server_token, "server-REDACTED");   /* resource accessToken -> server_token */
  CHECK_STR(cfg.token, "account-REDACTED");         /* the account token is never touched */
  CHECK_STR(pp_config_pms_token(&cfg), "server-REDACTED");
  CHECK(session_start_screen(cfg.token, cfg.server_url) == SCREEN_HOME);

  pp_server no_tok = { "http://192.168.1.10:32400", NULL, NULL, NULL };
  CHECK(session_apply_server(&cfg, &no_tok) == 0);
  CHECK_STR(cfg.server_url, "http://192.168.1.10:32400");
  CHECK(cfg.server_token[0] == '\0');               /* cleared: a stale one would be wrong */
  CHECK_STR(pp_config_pms_token(&cfg), "account-REDACTED");  /* falls back to the account token */
  pp_server empty_tok = { "http://h", "", NULL, NULL };
  CHECK(session_apply_server(&cfg, &empty_tok) == 0);
  CHECK(cfg.server_token[0] == '\0');
  CHECK_STR(cfg.token, "account-REDACTED");

  pp_server no_url = { NULL, "x", NULL, NULL };
  CHECK(session_apply_server(&cfg, &no_url) < 0);   /* nothing to persist */
  CHECK(session_apply_server(&cfg, NULL) < 0);
  CHECK(session_apply_server(NULL, &srv) < 0);
  char longurl[600];
  memset(longurl, 'a', sizeof longurl - 1);
  longurl[sizeof longurl - 1] = 0;
  pp_server too_long = { longurl, NULL, NULL, NULL };
  CHECK(session_apply_server(&cfg, &too_long) < 0);  /* would truncate */
  CHECK_STR(cfg.server_url, "http://h");            /* unchanged on failure */
}

/* Startup with a saved shared server: token + server_url + server_token goes
 * straight to Home and PMS calls use the server token; an old ini (no
 * server_token) still works off the account token. */
static void test_startup_with_server_token(void) {
  pp_config cfg;
  pp_config_defaults(&cfg);
  snprintf(cfg.token, sizeof cfg.token, "account-REDACTED");
  snprintf(cfg.server_url, sizeof cfg.server_url, "https://203-0-113-9.abc.plex.direct:32400");
  snprintf(cfg.server_token, sizeof cfg.server_token, "shared-REDACTED");
  CHECK(session_start_screen(cfg.token, cfg.server_url) == SCREEN_HOME);
  CHECK_STR(pp_config_pms_token(&cfg), "shared-REDACTED");

  cfg.server_token[0] = '\0';                        /* old ini file */
  CHECK(session_start_screen(cfg.token, cfg.server_url) == SCREEN_HOME);
  CHECK_STR(pp_config_pms_token(&cfg), "account-REDACTED");
}

static const char *label(const char *url, const char *name) {
  static char buf[128];
  pp_server s = { (char *)url, NULL, NULL, (char *)name };
  session_server_label(&s, buf, sizeof buf);
  return buf;
}

/* Owner: friendly names; fallback to the URL host; local vs remote when the URL shows it. */
static void test_server_label(void) {
  CHECK_STR(label("https://192-168-1-246.abc123.plex.direct:32400", "Living Room"), "Living Room (local)");
  CHECK_STR(label("https://203-0-113-10.abc123.plex.direct:32400", "Living Room"), "Living Room (remote)");
  CHECK_STR(label("https://192-168-1-246.abc123.plex.direct:32400", NULL), "192.168.1.246 (local)");
  CHECK_STR(label("http://10.0.0.5:32400", ""), "10.0.0.5 (local)");
  CHECK_STR(label("http://172.20.1.1:32400", NULL), "172.20.1.1 (local)");
  CHECK_STR(label("http://172.32.1.1:32400", NULL), "172.32.1.1 (remote)");   /* outside 172.16/12 */
  CHECK_STR(label("http://127.0.0.1:32400/", NULL), "127.0.0.1 (local)");
  CHECK_STR(label("http://203.0.113.20:32400", "Cabin"), "Cabin (remote)");
  CHECK_STR(label("https://plex.example.com:32400", "Office"), "Office");       /* can't tell */
  CHECK_STR(label("https://plex.example.com", NULL), "plex.example.com");
  CHECK_STR(label(NULL, "Den"), "Den");
  CHECK_STR(label(NULL, NULL), "(unknown server)");
  char small[8];
  pp_server s = { "http://10.0.0.5:32400", NULL, NULL, "A long friendly name" };
  session_server_label(&s, small, sizeof small);
  CHECK(strlen(small) == 7);                                                   /* truncates safely */
}

int main(void) {
  RUN(test_start_screen);
  RUN(test_apply_server_choice);
  RUN(test_startup_with_server_token);
  RUN(test_server_label);
  return TEST_RESULT();
}
