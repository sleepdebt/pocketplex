/* plex/auth.c: PIN login flow and server discovery.
 * Pure parsing (pp_parse_pin, pp_parse_servers) is fixture-tested; the
 * pp_auth_* / pp_discover_servers functions talk to plex.tv via http.c.
 */
#include "plex/internal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static char *dupstr(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

/* ---- pure: plex.tv/api/v2/pins ---- */

int pp_parse_pin(const char *json, char code_out[8], long *id_out,
                 char *token_out, size_t token_n) {
  if (!json || !code_out || !id_out) return PP_ERR_ARG;
  code_out[0] = '\0';
  if (token_out && token_n) token_out[0] = '\0';

  cJSON *root = cJSON_Parse(json);
  if (!root) return PP_ERR_PARSE;
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
  const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
  const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "authToken");
  if (!cJSON_IsNumber(id) || !cJSON_IsString(code) || !code->valuestring) {
    cJSON_Delete(root);
    return PP_ERR_PARSE;
  }
  *id_out = (long)id->valuedouble;
  snprintf(code_out, 8, "%s", code->valuestring);
  if (token_out && token_n && cJSON_IsString(token) && token->valuestring && token->valuestring[0])
    snprintf(token_out, token_n, "%s", token->valuestring);
  cJSON_Delete(root);
  return 0;
}

/* ---- pure: plex.tv/api/v2/resources ---- */

static int provides_server(const char *provides) {
  if (!provides) return 0;
  const char *p = provides;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    if (len == 6 && strncmp(p, "server", 6) == 0) return 1;
    if (!comma) break;
    p = comma + 1;
  }
  return 0;
}

int pp_parse_servers(const char *json, pp_server **out, int *count) {
  if (!json || !out || !count) return PP_ERR_ARG;
  *out = NULL;
  *count = 0;
  cJSON *root = cJSON_Parse(json);
  if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return PP_ERR_PARSE; }

  int n = 0;
  cJSON *node;
  cJSON_ArrayForEach(node, root) {
    const cJSON *provides = cJSON_GetObjectItemCaseSensitive(node, "provides");
    if (!cJSON_IsString(provides) || !provides_server(provides->valuestring)) continue;
    n++;
  }
  if (n == 0) { cJSON_Delete(root); return 0; }

  pp_server *servers = calloc((size_t)n, sizeof *servers);
  if (!servers) { cJSON_Delete(root); return PP_ERR_NOMEM; }
  int i = 0;
  cJSON_ArrayForEach(node, root) {
    const cJSON *provides = cJSON_GetObjectItemCaseSensitive(node, "provides");
    if (!cJSON_IsString(provides) || !provides_server(provides->valuestring)) continue;

    /* prefer the local=true connection */
    const char *url = NULL;
    const cJSON *conns = cJSON_GetObjectItemCaseSensitive(node, "connections");
    if (cJSON_IsArray(conns)) {
      const cJSON *c;
      cJSON_ArrayForEach(c, conns) {
        const cJSON *local = cJSON_GetObjectItemCaseSensitive(c, "local");
        const cJSON *uri = cJSON_GetObjectItemCaseSensitive(c, "uri");
        if (!cJSON_IsString(uri) || !uri->valuestring) continue;
        if (!url || cJSON_IsTrue(local)) url = uri->valuestring;
        if (cJSON_IsTrue(local)) break;
      }
    }
    const cJSON *access = cJSON_GetObjectItemCaseSensitive(node, "accessToken");
    const cJSON *cid = cJSON_GetObjectItemCaseSensitive(node, "clientIdentifier");
    if (!url) url = "";
    servers[i].url = dupstr(url);
    servers[i].token = dupstr(cJSON_IsString(access) && access->valuestring ? access->valuestring : "");
    servers[i].client_id = dupstr(cJSON_IsString(cid) && cid->valuestring ? cid->valuestring : "");
    if (!servers[i].url || !servers[i].token || !servers[i].client_id) {
      pp_servers_free(servers, i + 1);
      cJSON_Delete(root);
      return PP_ERR_NOMEM;
    }
    i++;
  }
  cJSON_Delete(root);
  *out = servers;
  *count = n;
  return 0;
}

void pp_servers_free(pp_server *servers, int count) {
  if (!servers) return;
  for (int i = 0; i < count; i++) {
    free(servers[i].url);
    free(servers[i].token);
    free(servers[i].client_id);
  }
  free(servers);
}

/* ---- network: PIN flow and discovery ---- */

#define PLEX_TV "https://plex.tv/api/v2"

int pp_auth_pin_start(char code_out[8], long *pin_id_out) {
  if (!code_out || !pin_id_out) return PP_ERR_ARG;
  char url[128];
  snprintf(url, sizeof url, "%s/pins?strong=false", PLEX_TV);
  pp_http_response r;
  int rc = pp_http_post(url, NULL, &r);
  if (rc == PP_OK) rc = pp_parse_pin(r.body, code_out, pin_id_out, NULL, 0);
  pp_http_free(&r);
  if (rc != PP_OK) LOGE("pin start failed: %d", rc);
  return rc;
}

int pp_auth_pin_poll(long pin_id, char *token_out, size_t n) {
  if (!token_out || n == 0) return PP_ERR_ARG;
  token_out[0] = '\0';
  char url[128];
  snprintf(url, sizeof url, "%s/pins/%ld", PLEX_TV, pin_id);
  pp_http_response r;
  int rc = pp_http_get(url, NULL, &r);
  if (rc != PP_OK) {
    int out = (rc == PP_ERR_HTTP) ? PP_ERR_AUTH : rc; /* 404 = PIN expired */
    pp_http_free(&r);
    return out;
  }
  char code[8];
  long id = 0;
  rc = pp_parse_pin(r.body, code, &id, token_out, n);
  pp_http_free(&r);
  if (rc != PP_OK) return rc;
  return token_out[0] ? 0 : 1; /* 0 = authorized, 1 = pending */
}

int pp_discover_servers(const char *token, pp_server **out, int *count) {
  if (!token || !token[0] || !out || !count) return PP_ERR_ARG;
  char url[160];
  snprintf(url, sizeof url, "%s/resources?includeHttps=1&includeRelay=0", PLEX_TV);
  pp_http_response r;
  int rc = pp_http_get(url, token, &r);
  if (rc == PP_OK) rc = pp_parse_servers(r.body, out, count);
  pp_http_free(&r);
  return rc;
}
