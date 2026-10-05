/* session.c: start-screen decision and server-choice persistence. */
#include "ui/session.h"

#include <string.h>

pp_screen_id session_start_screen(const char *token, const char *server_url) {
  if (!token || !token[0]) return SCREEN_LINK;
  if (!server_url || !server_url[0]) return SCREEN_SERVERS;
  return SCREEN_HOME;
}

int session_apply_server(pp_config *cfg, const pp_server *srv) {
  if (!cfg || !srv || !srv->url || !srv->url[0]) return -1;
  int has_tok = srv->token && srv->token[0];
  if (strlen(srv->url) >= sizeof cfg->server_url) return -1;
  if (has_tok && strlen(srv->token) >= sizeof cfg->token) return -1;
  strcpy(cfg->server_url, srv->url);
  if (has_tok) strcpy(cfg->token, srv->token);
  return 0;
}
