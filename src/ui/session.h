/* ui/session.h: start-screen decision and server-choice persistence.
 * Pure, no SDL, unit-tested. */
#ifndef PP_SESSION_H
#define PP_SESSION_H

#include "ui/ui.h"
#include "config/config.h"

/* No token -> Link; token only (fresh PIN link) -> Servers; token + url -> Home. */
pp_screen_id session_start_screen(const char *token, const char *server_url);

/* Record the chosen server in cfg: its url, and its access token (the PMS
 * token) in server_token — never the account token. A resource without a
 * token clears server_token so pp_config_pms_token falls back to the account
 * token. Returns 0, or -1 (cfg unchanged) if there is no url or a field
 * would not fit. The caller saves cfg. */
int session_apply_server(pp_config *cfg, const pp_server *srv);

/* Servers list label: the friendly name, else the URL host (a plex.direct
 * host is shown as its IP), plus " (local)" / " (remote)" when the address
 * shows it (private vs public IPv4). Always NUL-terminated. */
void session_server_label(const pp_server *srv, char *out, size_t n);

#endif /* PP_SESSION_H */
