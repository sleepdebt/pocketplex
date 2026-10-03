/* pp-cli: headless CLI against a real Plex server.
 *
 * Reads pocketplex.ini (POCKETPLEX_INI env overrides; then ./, then next to
 * the binary). If [plex] server_url + token are set, login/discovery are
 * skipped - the dev fallback from the spec.
 *
 * Commands:
 *   login                       PIN flow against plex.tv, then pick a server
 *   servers                     list servers on the account
 *   ls [key]                    sections, or children of key/ratingKey
 *   ondeck                      Continue Watching / On Deck
 *   url <ratingKey> [session]   transcode URL (plays in mpv); offset 0
 *   stop <session>              stop the PMS transcode session
 *   progress <ratingKey> <ms> [playing|paused|stopped]   report a resume point
 *   watched <ratingKey>         mark watched (scrobble)
 */
#include "config/config.h"
#include "plex/internal.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pp_config cfg;
static pp_server srv; /* filled from cfg */

static const char *err_name(int rc) {
  switch (rc) {
    case PP_OK: return "ok";
    case PP_ERR_NET: return "network";
    case PP_ERR_AUTH: return "auth (relink needed)";
    case PP_ERR_HTTP: return "http";
    case PP_ERR_PARSE: return "parse";
    case PP_ERR_ARG: return "bad argument";
    case PP_ERR_NOMEM: return "out of memory";
    default: return "unknown";
  }
}

static int fail(const char *what, int rc) {
  fprintf(stderr, "pp-cli: %s failed: %s (%d)\n", what, err_name(rc), rc);
  return 2;
}

static char *ini_path(const char *argv0) {
  static char path[512];
  const char *env = getenv("POCKETPLEX_INI");
  if (env && env[0]) return (char *)env;
  snprintf(path, sizeof path, "pocketplex.ini");
  FILE *f = fopen(path, "r");
  if (f) { fclose(f); return path; }
  const char *slash = strrchr(argv0, '/');
  if (slash) {
    snprintf(path, sizeof path, "%.*s/../pocketplex.ini", (int)(slash - argv0), argv0);
    f = fopen(path, "r");
    if (f) { fclose(f); return path; }
    snprintf(path, sizeof path, "%.*s/pocketplex.ini", (int)(slash - argv0), argv0);
    f = fopen(path, "r");
    if (f) { fclose(f); return path; }
  }
  snprintf(path, sizeof path, "pocketplex.ini");
  return path;
}

static void server_from_config(void) {
  srv.url = cfg.server_url;
  srv.token = cfg.token;
  srv.client_id = cfg.client_id;
}

static const char *kind_name(pp_kind k) {
  switch (k) {
    case PP_SECTION: return "section";
    case PP_SHOW: return "show";
    case PP_SEASON: return "season";
    case PP_EPISODE: return "episode";
    case PP_MOVIE: return "movie";
    case PP_ARTIST: return "artist";
    case PP_ALBUM: return "album";
    case PP_TRACK: return "track";
    case PP_DIR: return "dir";
  }
  return "?";
}

static void print_list(const pp_list *list) {
  for (int i = 0; i < list->count; i++) {
    const pp_item *it = &list->items[i];
    printf("%-8s %-8s %c %s", it->rating_key, kind_name(it->kind),
           it->watched ? 'W' : ' ', it->title);
    if (it->subtitle[0])
      printf("%s%s", (it->kind == PP_MOVIE || it->kind == PP_TRACK) ? "  (" : "  --  ",
             it->subtitle);
    if (it->subtitle[0] && (it->kind == PP_MOVIE || it->kind == PP_TRACK)) printf(")");
    putchar('\n');
    if (it->view_offset_ms > 0)
      printf("         resume %ldms / %ldms\n", it->view_offset_ms, it->duration_ms);
  }
  printf("(%d items)\n", list->count);
}

static int cmd_ls(int argc, char **argv) {
  pp_list list;
  int rc;
  if (argc < 3) {
    rc = pp_sections(&srv, &list);
  } else if (argv[2][0] == '/') {
    rc = pp_children(&srv, argv[2], &list); /* raw key from a previous listing */
  } else {
    /* long or non-numeric arg = ratingKey (common); short digits = section key */
    int rc2;
    if (strlen(argv[2]) > 3) {
      char key[128];
      snprintf(key, sizeof key, "/library/metadata/%s/children", argv[2]);
      rc2 = pp_children(&srv, key, &list);
      if (rc2 != PP_OK) { pp_list_free(&list); rc2 = pp_children(&srv, argv[2], &list); }
    } else {
      rc2 = pp_children(&srv, argv[2], &list);
    }
    rc = rc2;
  }
  if (rc != PP_OK) return fail("ls", rc);
  print_list(&list);
  pp_list_free(&list);
  return 0;
}

static int cmd_ondeck(void) {
  pp_list list;
  int rc = pp_on_deck(&srv, &list);
  if (rc != PP_OK) return fail("ondeck", rc);
  print_list(&list);
  pp_list_free(&list);
  return 0;
}

static int cmd_servers(void) {
  pp_server *servers = NULL;
  int count = 0;
  int rc = pp_discover_servers(cfg.token, &servers, &count);
  if (rc != PP_OK) return fail("servers", rc);
  for (int i = 0; i < count; i++)
    printf("%d  %s  (%s)\n", i, servers[i].url, servers[i].client_id);
  printf("(%d servers)\n", count);
  pp_servers_free(servers, count);
  return 0;
}

static int cmd_login(const char *ini) {
  if (cfg.token[0] && cfg.server_url[0] && !getenv("PP_FORCE_LOGIN")) {
    printf("already linked (token + server_url in %s); set PP_FORCE_LOGIN=1 to redo\n", ini);
    return 0;
  }
  char code[8];
  long pin_id = 0;
  int rc = pp_auth_pin_start(code, &pin_id);
  if (rc != PP_OK) return fail("pin start", rc);
  printf("PIN: %s\nEnter it at https://plex.tv/link ...\n", code);

  char token[256] = "";
  for (int tries = 0; tries < 450 && !token[0]; tries++) { /* PIN lasts 900 s */
    struct timespec ts = {2, 0};
    nanosleep(&ts, NULL);
    rc = pp_auth_pin_poll(pin_id, token, sizeof token);
    if (rc < 0) return fail("pin poll", rc);
    if (rc == 1) { putchar('.'); fflush(stdout); continue; }
  }
  if (!token[0]) { fprintf(stderr, "\npp-cli: PIN expired\n"); return 3; }
  printf("\nlinked.\n");
  snprintf(cfg.token, sizeof cfg.token, "%s", token);

  pp_server *servers = NULL;
  int count = 0;
  rc = pp_discover_servers(cfg.token, &servers, &count);
  if (rc == PP_OK && count > 0) {
    snprintf(cfg.server_url, sizeof cfg.server_url, "%s", servers[0].url);
    printf("server: %s\n", servers[0].url);
    pp_servers_free(servers, count);
  } else {
    fprintf(stderr, "warning: no server found (%s); set server_url in %s\n",
            err_name(rc), ini);
  }
  if (pp_config_save(&cfg, ini) == 0) printf("saved %s\n", ini);
  else fprintf(stderr, "pp-cli: could not save %s\n", ini);
  return 0;
}

static int cmd_url(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: pp-cli url <ratingKey> [session]\n"); return 1; }
  pp_item item;
  memset(&item, 0, sizeof item);
  snprintf(item.rating_key, sizeof item.rating_key, "%s", argv[2]);
  char session[64];
  if (argc >= 4) snprintf(session, sizeof session, "%s", argv[3]);
  else snprintf(session, sizeof session, "ppcli-%ld", (long)time(NULL));

  int w = 640, h = 480, kbps = 1500;
  if (strcmp(cfg.quality, "360p") == 0) { w = 640; h = 360; kbps = 750; }
  char url[1024];
  int rc = pp_transcode_url(&srv, &item, session, w, h, kbps, 0, url, sizeof url);
  if (rc != PP_OK) return fail("transcode url", rc);
  printf("%s\n", url);
  printf("session: %s\nstop with: pp-cli stop %s\n", session, session);
  return 0;
}

static int cmd_stop(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: pp-cli stop <session>\n"); return 1; }
  int rc = pp_transcode_stop(&srv, argv[2]);
  if (rc != PP_OK) return fail("transcode stop", rc);
  printf("stopped %s\n", argv[2]);
  return 0;
}

static int cmd_progress(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: pp-cli progress <ratingKey> <ms> [playing|paused|stopped]\n");
    return 1;
  }
  const char *state = (argc >= 5) ? argv[4] : "stopped";
  pp_item item;
  int rc = pp_fetch_item(&srv, argv[2], &item);
  if (rc != PP_OK) return fail("fetch item", rc);
  rc = pp_timeline(&srv, &item, state, strtol(argv[3], NULL, 10));
  if (rc != PP_OK) return fail("timeline", rc);
  printf("progress reported: %s at %s ms\n", item.title, argv[3]);
  return 0;
}

static int cmd_watched(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: pp-cli watched <ratingKey>\n"); return 1; }
  pp_item item;
  memset(&item, 0, sizeof item);
  snprintf(item.rating_key, sizeof item.rating_key, "%s", argv[2]);
  int rc = pp_scrobble(&srv, &item);
  if (rc != PP_OK) return fail("scrobble", rc);
  printf("marked watched: %s\n", argv[2]);
  return 0;
}

static void usage(void) {
  fputs("usage: pp-cli <command> [args]\n"
        "  login | servers | ls [key] | ondeck | url <rk> [session] |\n"
        "  stop <session> | progress <rk> <ms> [state] | watched <rk>\n",
        stderr);
}

int main(int argc, char **argv) {
  if (argc < 2) { usage(); return 1; }

  const char *ini = ini_path(argv[0]);
  pp_config_load(&cfg, ini);
  int had_client_id = cfg.client_id[0] != '\0';
  pp_config_ensure_client_id(&cfg);
  if (!had_client_id) { /* persist it: a new id per run would register a new Plex device */
    if (pp_config_save(&cfg, ini) != 0)
      fprintf(stderr, "pp-cli: warning: cannot write %s\n", ini);
  }
  server_from_config();

  int needs_server = strcmp(argv[1], "login") != 0 && strcmp(argv[1], "servers") != 0;
  if (needs_server && (!cfg.server_url[0] || !cfg.token[0])) {
    fprintf(stderr, "pp-cli: %s has no server_url/token; run 'pp-cli login' first\n", ini);
    return 2;
  }

  int rc = pp_init(cfg.client_id);
  if (rc != PP_OK) return fail("init", rc);
  atexit(pp_cleanup);

  if (strcmp(argv[1], "login") == 0) rc = cmd_login(ini);
  else if (strcmp(argv[1], "servers") == 0) rc = cmd_servers();
  else if (strcmp(argv[1], "ls") == 0) rc = cmd_ls(argc, argv);
  else if (strcmp(argv[1], "ondeck") == 0) rc = cmd_ondeck();
  else if (strcmp(argv[1], "url") == 0) rc = cmd_url(argc, argv);
  else if (strcmp(argv[1], "stop") == 0) rc = cmd_stop(argc, argv);
  else if (strcmp(argv[1], "progress") == 0) rc = cmd_progress(argc, argv);
  else if (strcmp(argv[1], "watched") == 0) rc = cmd_watched(argc, argv);
  else { usage(); rc = 1; }

  return rc;
}
