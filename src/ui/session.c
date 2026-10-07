/* session.c: start-screen decision and server-choice persistence. */
#include "ui/session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

pp_screen_id session_start_screen(const char *token, const char *server_url) {
  if (!token || !token[0]) return SCREEN_LINK;
  if (!server_url || !server_url[0]) return SCREEN_SERVERS;
  return SCREEN_HOME;
}

int session_apply_server(pp_config *cfg, const pp_server *srv) {
  if (!cfg || !srv || !srv->url || !srv->url[0]) return -1;
  const char *tok = srv->token ? srv->token : "";
  if (strlen(srv->url) >= sizeof cfg->server_url) return -1;
  if (strlen(tok) >= sizeof cfg->server_token) return -1;
  strcpy(cfg->server_url, srv->url);
  strcpy(cfg->server_token, tok);  /* the account token stays untouched */
  return 0;
}

/* Host part of a URL into out ("https://h:1/x" -> "h"). */
static void url_host(const char *url, char *out, size_t n) {
  out[0] = '\0';
  if (!url) return;
  const char *p = strstr(url, "://");
  p = p ? p + 3 : url;
  size_t len = strcspn(p, ":/?");
  if (len >= n) len = n - 1;
  memcpy(out, p, len);
  out[len] = '\0';
}

/* "192-168-1-2.<hash>.plex.direct" -> "192.168.1.2" (in place). */
static void plex_direct_to_ip(char *host) {
  size_t len = strlen(host);
  const char *sfx = ".plex.direct";
  size_t sl = strlen(sfx);
  if (len <= sl || strcmp(host + len - sl, sfx) != 0) return;
  char *dot = strchr(host, '.');
  if (!dot) return;
  *dot = '\0';
  for (char *c = host; *c; c++) if (*c == '-') *c = '.';
}

/* 1 private, 0 public, -1 not a dotted IPv4 address. */
static int ipv4_private(const char *host) {
  int a, b, c, d;
  char tail;
  if (sscanf(host, "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) != 4) return -1;
  if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return -1;
  if (a == 10 || a == 127) return 1;
  if (a == 192 && b == 168) return 1;
  if (a == 172 && b >= 16 && b <= 31) return 1;
  if (a == 169 && b == 254) return 1;
  return 0;
}

void session_server_label(const pp_server *srv, char *out, size_t n) {
  if (!out || n == 0) return;
  char host[256];
  url_host(srv ? srv->url : NULL, host, sizeof host);
  plex_direct_to_ip(host);
  const char *base = srv && srv->name && srv->name[0] ? srv->name : host;
  if (!base[0]) base = "(unknown server)";
  int priv = host[0] ? ipv4_private(host) : -1;
  snprintf(out, n, "%s%s", base, priv == 1 ? " (local)" : priv == 0 ? " (remote)" : "");
}
