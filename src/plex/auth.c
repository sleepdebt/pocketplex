/* plex/auth.c: PIN login flow and server discovery.
 * Pure parsing (pp_parse_pin, pp_parse_servers, pp_parse_resources,
 * pp_rank_conns, pp_choose_conn) is fixture-tested; the pp_auth_* /
 * pp_discover_servers* functions talk to plex.tv / PMS via http.c.
 */
#include "plex/internal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static int json_bool(const cJSON *node, const char *field) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(node, field);
  return cJSON_IsTrue(v) ? 1 : 0;
}

static void json_str(const cJSON *node, const char *field, char *out, size_t n) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(node, field);
  snprintf(out, n, "%s",
           (cJSON_IsString(v) && v->valuestring) ? v->valuestring : "");
}

int pp_parse_resources(const char *json, pp_resource *res, int max, int *count) {
  if (!json || !res || max <= 0 || !count) return PP_ERR_ARG;
  memset(res, 0, (size_t)max * sizeof *res);
  *count = 0;
  cJSON *root = cJSON_Parse(json);
  if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return PP_ERR_PARSE; }

  cJSON *node;
  cJSON_ArrayForEach(node, root) {
    if (*count >= max) break;
    char provides[128];
    json_str(node, "provides", provides, sizeof provides);
    if (!provides_server(provides)) continue;
    pp_resource *r = &res[(*count)++];
    json_str(node, "name", r->name, sizeof r->name);
    json_str(node, "accessToken", r->token, sizeof r->token);
    json_str(node, "clientIdentifier", r->client_id, sizeof r->client_id);
    r->owned = json_bool(node, "owned");
    r->public_address_matches = json_bool(node, "publicAddressMatches");
    const cJSON *conns = cJSON_GetObjectItemCaseSensitive(node, "connections");
    if (!cJSON_IsArray(conns)) continue;
    const cJSON *c;
    cJSON_ArrayForEach(c, conns) {
      if (r->conn_count >= PP_MAX_CONNS) break;
      char uri[sizeof r->conns[0].uri];
      json_str(c, "uri", uri, sizeof uri);
      if (!uri[0]) continue; /* no uri: unusable */
      pp_conn *cn = &r->conns[r->conn_count++];
      snprintf(cn->uri, sizeof cn->uri, "%s", uri);
      cn->local = json_bool(c, "local");
      cn->relay = json_bool(c, "relay");
    }
  }
  cJSON_Delete(root);
  return 0;
}

int pp_rank_conns(const pp_resource *res, int order[PP_MAX_CONNS]) {
  if (!res) return 0;
  int local_ok = res->owned || res->public_address_matches;
  int score[PP_MAX_CONNS];
  for (int i = 0; i < res->conn_count; i++) {
    const pp_conn *c = &res->conns[i];
    if (c->relay) score[i] = 2;
    else if (c->local) score[i] = local_ok ? 0 : 3; /* 3 = not allowed */
    else score[i] = 1;                              /* remote direct */
  }
  int n = 0;
  for (int want = 0; want <= 3; want++)
    for (int i = 0; i < res->conn_count; i++)
      if (score[i] == want) order[n++] = i;
  return n;
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int pp_choose_conn(const pp_resource *res, pp_probe_fn probe, void *ud,
                   long budget_ms, int *reachable_out, int *class_out) {
  if (reachable_out) *reachable_out = 0;
  if (class_out) *class_out = PP_CONN_REMOTE;
  if (!res || res->conn_count == 0) return -1;

  int order[PP_MAX_CONNS];
  int n = pp_rank_conns(res, order);
  if (n == 0) return -1;

  long deadline = now_ms() + budget_ms;
  for (int k = 0; k < n; k++) {
    if (!probe || now_ms() >= deadline) break; /* out of budget: stop probing */
    const pp_conn *c = &res->conns[order[k]];
    if (probe(c->uri, res->token, ud)) {
      if (reachable_out) *reachable_out = 1;
      if (class_out)
        *class_out = c->relay ? PP_CONN_RELAY : c->local ? PP_CONN_LOCAL : PP_CONN_REMOTE;
      return order[k];
    }
  }
  /* nothing answered (or no budget): best-ranked URL so the UI can show an error */
  const pp_conn *best = &res->conns[order[0]];
  if (class_out)
    *class_out = best->relay ? PP_CONN_RELAY : best->local ? PP_CONN_LOCAL : PP_CONN_REMOTE;
  return order[0];
}

int pp_parse_servers(const char *json, pp_server **out, int *count) {
  if (!json || !out || !count) return PP_ERR_ARG;
  *out = NULL;
  *count = 0;
  pp_resource res[PP_MAX_RESOURCES];
  int n = 0;
  int rc = pp_parse_resources(json, res, PP_MAX_RESOURCES, &n);
  if (rc != PP_OK) return rc;
  if (n == 0) return 0;

  pp_server *servers = calloc((size_t)n, sizeof *servers);
  if (!servers) return PP_ERR_NOMEM;
  for (int i = 0; i < n; i++) {
    /* best-ranked connection, no probing (pure function; tests use this) */
    int order[PP_MAX_CONNS];
    int ranked = pp_rank_conns(&res[i], order);
    const char *url = ranked > 0 ? res[i].conns[order[0]].uri : "";
    servers[i].url = dupstr(url);
    servers[i].token = dupstr(res[i].token);
    servers[i].client_id = dupstr(res[i].client_id);
    servers[i].name = res[i].name[0] ? dupstr(res[i].name) : NULL;
    if (!servers[i].url || !servers[i].token || !servers[i].client_id ||
        (res[i].name[0] && !servers[i].name)) {
      pp_servers_free(servers, i + 1);
      return PP_ERR_NOMEM;
    }
  }
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
    free(servers[i].name); /* NULL-safe */
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
  int written = snprintf(url, sizeof url, "%s/pins/%ld", PLEX_TV, pin_id);
  if (written < 0 || (size_t)written >= sizeof url) return PP_ERR_ARG;
  pp_http_response r;
  int rc = pp_http_get_status(url, NULL, &r);
  if (rc != PP_OK) { pp_http_free(&r); return rc; } /* transport failure */
  long status = r.status;
  if (status == 404) { pp_http_free(&r); return PP_ERR_AUTH; } /* PIN expired */
  if (status == 401 || status == 403) { pp_http_free(&r); return PP_ERR_AUTH; }
  if (status < 200 || status >= 300) { /* e.g. 429 rate limit, 5xx */
    pp_http_free(&r);
    return PP_ERR_HTTP;
  }
  char code[8];
  long id = 0;
  rc = pp_parse_pin(r.body, code, &id, token_out, n);
  pp_http_free(&r);
  if (rc != PP_OK) return rc;
  return token_out[0] ? 0 : 1; /* 0 = authorized, 1 = pending */
}

int pp_discover_servers_ex(const char *token, pp_probe_fn probe, void *ud,
                           pp_server_info **out, int *count) {
  if (!token || !token[0] || !out || !count) return PP_ERR_ARG;
  *out = NULL;
  *count = 0;
  char url[160];
  snprintf(url, sizeof url, "%s/resources?includeHttps=1&includeRelay=1", PLEX_TV);
  pp_http_response r;
  int rc = pp_http_get(url, token, &r);
  if (rc != PP_OK) { pp_http_free(&r); return rc; }
  pp_resource res[PP_MAX_RESOURCES];
  rc = pp_parse_resources(r.body, res, PP_MAX_RESOURCES, count);
  pp_http_free(&r);
  if (rc != PP_OK || *count == 0) return rc;

  pp_server_info *servers = calloc((size_t)*count, sizeof *servers);
  if (!servers) { *count = 0; return PP_ERR_NOMEM; }
  for (int i = 0; i < *count; i++) {
    int reachable = 0, cls = PP_CONN_REMOTE;
    int idx = pp_choose_conn(&res[i], probe, ud, PP_PROBE_BUDGET_MS, &reachable, &cls);
    servers[i].server.url = dupstr(idx >= 0 ? res[i].conns[idx].uri : "");
    servers[i].server.token = dupstr(res[i].token);
    servers[i].server.client_id = dupstr(res[i].client_id);
    servers[i].server.name = res[i].name[0] ? dupstr(res[i].name) : NULL;
    servers[i].conn_class = cls;
    servers[i].reachable = reachable;
    if (!servers[i].server.url || !servers[i].server.token ||
        !servers[i].server.client_id || (res[i].name[0] && !servers[i].server.name)) {
      pp_server_infos_free(servers, i + 1);
      *out = NULL;
      *count = 0;
      return PP_ERR_NOMEM;
    }
  }
  *out = servers;
  return 0;
}

void pp_server_infos_free(pp_server_info *servers, int count) {
  if (!servers) return;
  for (int i = 0; i < count; i++) {
    free(servers[i].server.url);
    free(servers[i].server.token);
    free(servers[i].server.client_id);
    free(servers[i].server.name);
  }
  free(servers);
}

int pp_discover_servers(const char *token, pp_server **out, int *count) {
  if (!token || !token[0] || !out || !count) return PP_ERR_ARG;
  pp_server_info *servers = NULL;
  int n = 0;
  int rc = pp_discover_servers_ex(token, pp_http_probe, NULL, &servers, &n);
  if (rc != PP_OK) return rc;
  /* shrink to the pp_server contract view: move the heap strings over,
   * free only the info shells (pp_servers_free frees the four fields) */
  pp_server *view = calloc((size_t)n, sizeof *view);
  if (!view) { pp_server_infos_free(servers, n); return PP_ERR_NOMEM; }
  for (int i = 0; i < n; i++) {
    view[i] = servers[i].server;
    servers[i].server.url = NULL;
    servers[i].server.token = NULL;
    servers[i].server.client_id = NULL;
    servers[i].server.name = NULL;
  }
  pp_server_infos_free(servers, n);
  *out = view;
  *count = n;
  return 0;
}
