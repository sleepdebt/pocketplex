/* plex/library.c: sections / children / on-deck.
 * Pure parsing lives in pp_parse_items (fixture-tested); the pp_* functions
 * fetch from the server and hand the body to the parser.
 */
#include "plex/internal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static void scpy(char *dst, size_t n, const char *src) {
  if (!src) src = "";
  snprintf(dst, n, "%s", src);
}

/* MediaContainer.totalSize (0 when absent) so paging can stop at the end. */
static long parse_total_size(const char *json) {
  cJSON *root = cJSON_Parse(json);
  if (!root) return 0;
  cJSON *mc = cJSON_GetObjectItemCaseSensitive(root, "MediaContainer");
  long total = 0;
  if (mc) {
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(mc, "totalSize");
    if (cJSON_IsNumber(t)) total = (long)t->valuedouble;
  }
  cJSON_Delete(root);
  return total;
}

static const char *item_string(const cJSON *node, const char *field) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(node, field);
  if (!v) return NULL;
  if (cJSON_IsString(v) && v->valuestring) return v->valuestring;
  if (cJSON_IsNumber(v)) return NULL; /* handled by item_number callers */
  return NULL;
}

static long item_number(const cJSON *node, const char *field, long dflt) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(node, field);
  if (!v) return dflt;
  if (cJSON_IsNumber(v)) return (long)v->valuedouble;
  if (cJSON_IsString(v) && v->valuestring && v->valuestring[0])
    return strtol(v->valuestring, NULL, 10);
  return dflt;
}

static pp_kind kind_from_type(const char *type, int is_directory) {
  if (!type) return is_directory ? PP_SECTION : PP_DIR;
  if (strcmp(type, "movie") == 0) return PP_MOVIE;
  if (strcmp(type, "show") == 0) return PP_SHOW;
  if (strcmp(type, "season") == 0) return PP_SEASON;
  if (strcmp(type, "episode") == 0) return PP_EPISODE;
  if (strcmp(type, "artist") == 0) return PP_ARTIST;
  if (strcmp(type, "album") == 0) return PP_ALBUM;
  if (strcmp(type, "track") == 0) return PP_TRACK;
  return is_directory ? PP_SECTION : PP_DIR;
}

static void fill_subtitle(pp_item *item, const cJSON *node, pp_kind kind) {
  if (kind == PP_EPISODE) {
    long pidx = item_number(node, "parentIndex", -1);
    long idx = item_number(node, "index", -1);
    if (pidx > 0 && idx > 0) {
      snprintf(item->subtitle, sizeof item->subtitle, "S%02ldE%02ld", pidx, idx);
      return;
    }
  }
  if (kind == PP_SHOW || kind == PP_SEASON) {
    long leaf = item_number(node, "leafCount", 0);
    long viewed = item_number(node, "viewedLeafCount", 0);
    if (leaf > 0) {
      snprintf(item->subtitle, sizeof item->subtitle, "%ld / %ld episodes", viewed, leaf);
      return;
    }
  }
  const char *parent = item_string(node, "parentTitle");
  if (!parent) parent = item_string(node, "grandparentTitle");
  if (!parent) parent = "";
  scpy(item->subtitle, sizeof item->subtitle, parent);
}

static int fill_item(pp_item *item, const cJSON *node) {
  const char *rk = item_string(node, "ratingKey");
  if (!rk) rk = item_string(node, "key"); /* Directory entries have key only */
  scpy(item->rating_key, sizeof item->rating_key, rk);

  const cJSON *key = cJSON_GetObjectItemCaseSensitive(node, "key");
  if (cJSON_IsString(key) && key->valuestring)
    scpy(item->key, sizeof item->key, key->valuestring);
  else
    scpy(item->key, sizeof item->key, rk);

  const char *title = item_string(node, "title");
  scpy(item->title, sizeof item->title, title ? title : "");

  int is_dir = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(node, "directory"));
  item->kind = kind_from_type(item_string(node, "type"), is_dir);
  item->year = (int)item_number(node, "year", 0);
  item->duration_ms = item_number(node, "duration", 0);
  item->view_offset_ms = item_number(node, "viewOffset", 0);
  item->leaf_count = (int)item_number(node, "leafCount", 0);
  item->viewed_leaf_count = (int)item_number(node, "viewedLeafCount", 0);
  /* Leaf items: viewCount > 0 means watched. Containers (show/season/artist/
   * album): PMS reports a play count in viewCount, so "watched" is derived
   * from having seen every leaf instead. */
  if (item->kind == PP_SHOW || item->kind == PP_SEASON ||
      item->kind == PP_ARTIST || item->kind == PP_ALBUM)
    item->watched = item->leaf_count > 0 && item->viewed_leaf_count >= item->leaf_count;
  else
    item->watched = item_number(node, "viewCount", 0) > 0;
  scpy(item->summary, sizeof item->summary, item_string(node, "summary"));
  fill_subtitle(item, node, item->kind);
  return 0;
}

int pp_parse_items(const char *json, pp_list *out) {
  if (!json || !out) return PP_ERR_ARG;
  out->items = NULL;
  out->count = 0;
  cJSON *root = cJSON_Parse(json);
  if (!root) return PP_ERR_PARSE;
  cJSON *mc = cJSON_GetObjectItemCaseSensitive(root, "MediaContainer");
  if (!mc) { cJSON_Delete(root); return PP_ERR_PARSE; }

  /* sections use "Directory", everything else "Metadata" */
  cJSON *arr = cJSON_GetObjectItemCaseSensitive(mc, "Metadata");
  int is_dir_array = 0;
  if (!arr) {
    arr = cJSON_GetObjectItemCaseSensitive(mc, "Directory");
    is_dir_array = 1;
  }
  int n = 0;
  if (arr) {
    cJSON *node;
    cJSON_ArrayForEach(node, arr) n++;
  }
  if (n == 0) { cJSON_Delete(root); return 0; }

  pp_item *items = calloc((size_t)n, sizeof *items);
  if (!items) { cJSON_Delete(root); return PP_ERR_NOMEM; }
  int i = 0;
  cJSON *node;
  cJSON_ArrayForEach(node, arr) {
    fill_item(&items[i], node);
    if (is_dir_array) items[i].kind = PP_SECTION;
    i++;
  }
  cJSON_Delete(root);
  out->items = items;
  out->count = n;
  return 0;
}

void pp_list_free(pp_list *list) {
  if (!list) return;
  free(list->items);
  list->items = NULL;
  list->count = 0;
}

/* ---- network wrappers ---- */

static int fetch_list(const char *url_first_page, const pp_server *srv, pp_list *out,
                      int page) {
  out->items = NULL;
  out->count = 0;
  if (!srv || !srv->url || !srv->token) return PP_ERR_ARG;
  if (!page) {
    pp_http_response r;
    int rc = pp_http_get(url_first_page, srv->token, &r);
    if (rc != PP_OK) { pp_http_free(&r); return rc; }
    rc = pp_parse_items(r.body, out);
    pp_http_free(&r);
    return rc;
  }

  /* Paged fetch: 50 at a time. Stops on a short page, at the server's
   * totalSize, or after MAX_PAGES (defensive: an endpoint that ignores the
   * container params must not loop forever). */
  enum { PAGE_SIZE = 50, MAX_PAGES = 400 };
  int total = 0, start = 0;
  long total_size = -1; /* unknown until the first response */
  pp_item *all = NULL;
  for (int page_no = 0; page_no < MAX_PAGES; page_no++) {
    char url[1024];
    int written = snprintf(url, sizeof url, "%s%sX-Plex-Container-Start=%d&X-Plex-Container-Size=%d",
                           url_first_page, strchr(url_first_page, '?') ? "&" : "?",
                           start, PAGE_SIZE);
    if (written < 0 || (size_t)written >= sizeof url) { free(all); return PP_ERR_ARG; }
    pp_http_response r;
    int rc = pp_http_get(url, srv->token, &r);
    if (rc != PP_OK) { pp_http_free(&r); free(all); return rc; }
    pp_list part;
    rc = pp_parse_items(r.body, &part);
    if (rc == PP_OK && total_size < 0) total_size = parse_total_size(r.body);
    pp_http_free(&r);
    if (rc != PP_OK) { free(all); return rc; }
    if (part.count == 0) { pp_list_free(&part); break; }
    pp_item *grown = realloc(all, (size_t)(total + part.count) * sizeof *all);
    if (!grown) { pp_list_free(&part); free(all); return PP_ERR_NOMEM; }
    all = grown;
    memcpy(all + total, part.items, (size_t)part.count * sizeof *part.items);
    total += part.count;
    int got = part.count;
    pp_list_free(&part);
    if (got < PAGE_SIZE) break;
    start += PAGE_SIZE;
    if (total_size > 0 && start >= total_size) break;
  }
  out->items = all;
  out->count = total;
  return 0;
}

int pp_sections(const pp_server *srv, pp_list *out) {
  if (!srv || !srv->url || !srv->token || !out) return PP_ERR_ARG;
  char url[512];
  snprintf(url, sizeof url, "%s/library/sections", srv->url);
  return fetch_list(url, srv, out, 0);
}

int pp_children(const pp_server *srv, const char *key, pp_list *out) {
  if (!srv || !srv->url || !srv->token || !out) return PP_ERR_ARG;
  if (!key || !key[0]) return PP_ERR_ARG;
  char url[768];
  if (key[0] == '/')
    snprintf(url, sizeof url, "%s%s", srv->url, key);
  else /* section key like "1" -> its contents */
    snprintf(url, sizeof url, "%s/library/sections/%s/all", srv->url, key);
  return fetch_list(url, srv, out, 1);
}

int pp_on_deck(const pp_server *srv, pp_list *out) {
  if (!srv || !srv->url || !srv->token || !out) return PP_ERR_ARG;
  char url[512];
  snprintf(url, sizeof url, "%s/library/onDeck", srv->url);
  return fetch_list(url, srv, out, 0);
}

int pp_fetch_item(const pp_server *srv, const char *rating_key, pp_item *out) {
  if (!srv || !srv->url || !srv->token || !rating_key || !rating_key[0] || !out)
    return PP_ERR_ARG;
  memset(out, 0, sizeof *out);
  char url[512];
  int written = snprintf(url, sizeof url, "%s/library/metadata/%s", srv->url, rating_key);
  if (written < 0 || (size_t)written >= sizeof url) return PP_ERR_ARG;
  pp_http_response r;
  int rc = pp_http_get(url, srv->token, &r);
  if (rc != PP_OK) { pp_http_free(&r); return rc; }
  pp_list list;
  rc = pp_parse_items(r.body, &list);
  pp_http_free(&r);
  if (rc != PP_OK) return rc;
  if (list.count < 1) { pp_list_free(&list); return PP_ERR_PARSE; }
  *out = list.items[0]; /* by value: pp_item has no pointers */
  pp_list_free(&list);
  return 0;
}
