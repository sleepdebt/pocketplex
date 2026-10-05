/* ui/session.h: start-screen decision and server-choice persistence.
 * Pure, no SDL, unit-tested. */
#ifndef PP_SESSION_H
#define PP_SESSION_H

#include "ui/ui.h"
#include "config/config.h"

/* No token -> Link; token only (fresh PIN link) -> Servers; token + url -> Home. */
pp_screen_id session_start_screen(const char *token, const char *server_url);

/* Record the chosen server in cfg: its url, and its access token when the
 * resource has one. Returns 0, or -1 (cfg unchanged) if there is no url or a
 * field would not fit. The caller saves cfg. */
int session_apply_server(pp_config *cfg, const pp_server *srv);

#endif /* PP_SESSION_H */
