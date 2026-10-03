#include "plex/internal.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_fixture(const char *name) {
  char path[512];
  snprintf(path, sizeof path, "tests/fixtures/%s", name);
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "  missing fixture %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)len + 1);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { fprintf(stderr, "  read failed %s\n", path); exit(1); }
  buf[len] = '\0';
  fclose(f);
  return buf;
}

static void free_fixture(char *s) { free(s); }

/* ---------- pins ---------- */

static void test_parse_pin_created(void) {
  char code[8], token[64];
  long id = 0;
  char *j = read_fixture("pin_created.json");
  CHECK(pp_parse_pin(j, code, &id, token, sizeof token) == 0);
  CHECK_STR(code, "ABCD");
  CHECK(id > 0);
  CHECK(token[0] == '\0'); /* pending: no token yet */
  free_fixture(j);
}

static void test_parse_pin_authorized(void) {
  char code[8], token[64];
  long id = 0;
  char *j = read_fixture("pin_authorized.json");
  CHECK(pp_parse_pin(j, code, &id, token, sizeof token) == 0);
  CHECK_STR(token, "REDACTED");
  free_fixture(j);
}

static void test_parse_pin_bad_json(void) {
  char code[8], token[64];
  long id = 0;
  CHECK(pp_parse_pin("{\"id\":", code, &id, token, sizeof token) == PP_ERR_PARSE);
  CHECK(pp_parse_pin("{}", code, &id, token, sizeof token) == PP_ERR_PARSE);
  CHECK(pp_parse_pin(NULL, code, &id, token, sizeof token) == PP_ERR_ARG);
}

/* ---------- resources / servers ---------- */

static void test_parse_servers(void) {
  pp_server *servers = NULL;
  int count = 0;
  char *j = read_fixture("resources.json");
  CHECK(pp_parse_servers(j, &servers, &count) == 0);
  CHECK(count == 2); /* only resources providing "server" */
  CHECK(servers != NULL);
  /* local=true connection preferred */
  CHECK(strstr(servers[0].url, "plex.direct") != NULL);
  CHECK(strncmp(servers[0].url, "https://", 8) == 0);
  CHECK_STR(servers[0].token, "REDACTED");
  CHECK(strlen(servers[0].client_id) > 0);
  pp_servers_free(servers, count);
  free_fixture(j);
}

static void test_parse_servers_bad(void) {
  pp_server *servers;
  int count;
  CHECK(pp_parse_servers("[]", &servers, &count) == 0); /* valid: no servers */
  CHECK(count == 0);
  CHECK(pp_parse_servers("{}", &servers, &count) == PP_ERR_PARSE);
  CHECK(pp_parse_servers("not json", &servers, &count) == PP_ERR_PARSE);
  CHECK(pp_parse_servers(NULL, &servers, &count) == PP_ERR_ARG);
}

/* ---------- items ---------- */

static void test_parse_sections(void) {
  pp_list list;
  char *j = read_fixture("sections.json");
  CHECK(pp_parse_items(j, &list) == 0);
  CHECK(list.count == 4);
  CHECK_STR(list.items[0].rating_key, "1");
  CHECK(list.items[0].kind == PP_SECTION);
  CHECK_STR(list.items[0].title, "Movies");
  CHECK_STR(list.items[1].title, "TV Shows");
  CHECK_STR(list.items[2].title, "Music");
  /* section key must be usable in pp_children: "1" is stored raw */
  CHECK_STR(list.items[0].key, "1");
  pp_list_free(&list);
  free_fixture(j);
}

static void test_parse_shows(void) {
  pp_list list;
  char *j = read_fixture("section_tv_all.json");
  CHECK(pp_parse_items(j, &list) == 0);
  CHECK(list.count == 3);
  const pp_item *show = &list.items[1]; /* The Americans */
  CHECK(show->kind == PP_SHOW);
  CHECK_STR(show->rating_key, "53782");
  CHECK_STR(show->key, "/library/metadata/53782/children");
  CHECK(show->leaf_count == 75);
  CHECK(show->viewed_leaf_count == 4);
  CHECK(show->watched == 0); /* 4 of 75 seen */
  CHECK(list.items[0].watched == 1); /* 8 of 8 seen */
  pp_list_free(&list);
  free_fixture(j);
}

static void test_parse_seasons(void) {
  pp_list list;
  char *j = read_fixture("show_children.json");
  CHECK(pp_parse_items(j, &list) == 0);
  CHECK(list.count == 2);
  CHECK(list.items[0].kind == PP_SEASON);
  CHECK_STR(list.items[0].title, "Season 1");
  CHECK(list.items[0].leaf_count == 13);
  pp_list_free(&list);
  free_fixture(j);
}

static void test_parse_episodes(void) {
  pp_list list;
  char *j = read_fixture("season_children.json");
  CHECK(pp_parse_items(j, &list) == 0);
  CHECK(list.count == 4);
  const pp_item *ep = &list.items[0];
  CHECK(ep->kind == PP_EPISODE);
  CHECK_STR(ep->rating_key, "53834");
  CHECK_STR(ep->title, "Pilot");
  CHECK(ep->duration_ms == 4155167);
  CHECK(ep->view_offset_ms == 0); /* absent = 0 */
  CHECK(ep->watched == 1);        /* viewCount 1 */
  CHECK(ep->year == 2013);
  CHECK(strlen(ep->summary) > 10);
  pp_list_free(&list);
  free_fixture(j);
}

static void test_parse_ondeck(void) {
  pp_list list;
  char *j = read_fixture("ondeck.json");
  CHECK(pp_parse_items(j, &list) == 0);
  CHECK(list.count == 3);
  CHECK(list.items[0].kind == PP_MOVIE);
  CHECK(list.items[0].view_offset_ms == 1865674);
  CHECK(list.items[0].duration_ms == 7201472);
  pp_list_free(&list);
  free_fixture(j);
}

static void test_parse_music_kinds(void) {
  pp_list artists, albums, tracks;
  char *ja = read_fixture("artists.json");
  char *jb = read_fixture("album_children_of_artist.json");
  char *jc = read_fixture("track_children_of_album.json");
  CHECK(pp_parse_items(ja, &artists) == 0);
  CHECK(artists.count == 2 && artists.items[0].kind == PP_ARTIST);
  CHECK(pp_parse_items(jb, &albums) == 0);
  CHECK(albums.count == 2 && albums.items[0].kind == PP_ALBUM);
  CHECK(pp_parse_items(jc, &tracks) == 0);
  CHECK(tracks.count == 4 && tracks.items[0].kind == PP_TRACK);
  CHECK(tracks.items[0].duration_ms == 376946);
  pp_list_free(&artists); pp_list_free(&albums); pp_list_free(&tracks);
  free_fixture(ja); free_fixture(jb); free_fixture(jc);
}

static void test_parse_single_movie(void) {
  pp_list list;
  char *j = read_fixture("movie.json");
  CHECK(pp_parse_items(j, &list) == 0);
  CHECK(list.count == 1);
  CHECK(list.items[0].kind == PP_MOVIE);
  CHECK_STR(list.items[0].title, "Home Alone 2: Lost in New York");
  CHECK(list.items[0].year == 1992);
  CHECK(list.items[0].view_offset_ms == 1865674);
  pp_list_free(&list);
  free_fixture(j);
}

static void test_parse_items_bad(void) {
  pp_list list;
  CHECK(pp_parse_items("not json", &list) == PP_ERR_PARSE);
  CHECK(pp_parse_items("{\"MediaContainer\":{}}", &list) == 0);
  CHECK(list.count == 0 && list.items == NULL);
  CHECK(pp_parse_items(NULL, &list) == PP_ERR_ARG);
}

static void test_list_free_null_safe(void) {
  pp_list_free(NULL);
  pp_list empty = {NULL, 0};
  pp_list_free(&empty);
  CHECK(1);
}

/* ---------- transcode URL ---------- */

static void test_build_transcode_url_offset_zero(void) {
  pp_server srv = {"http://192.0.2.10:32400", "tok123", "cid"};
  char url[1024];
  CHECK(pp_build_transcode_url(&srv, "/library/metadata/53834", "sess-1",
                               640, 480, 1500, 0, url, sizeof url) == 0);
  CHECK_STR(url,
    "http://192.0.2.10:32400/video/:/transcode/universal/start.m3u8"
    "?path=/library/metadata/53834"
    "&mediaIndex=0&partIndex=0"
    "&protocol=hls&fastSeek=1&copyts=1"
    "&offset=0"
    "&maxVideoBitrate=1500"
    "&videoResolution=640x480"
    "&X-Plex-Platform=Chrome"
    "&directPlay=0&directStream=0"
    "&subtitles=burn"
    "&session=sess-1"
    "&X-Plex-Token=tok123");
}

static void test_build_transcode_url_offset_seconds(void) {
  pp_server srv = {"http://192.0.2.10:32400", "tok123", "cid"};
  char url[1024];
  CHECK(pp_build_transcode_url(&srv, "/library/metadata/53834", "s2",
                               640, 360, 750, 90500, url, sizeof url) == 0);
  CHECK(strstr(url, "offset=90") != NULL);   /* 90500 ms -> 90 s */
  CHECK(strstr(url, "videoResolution=640x360") != NULL);
  CHECK(strstr(url, "maxVideoBitrate=750") != NULL);
}

static void test_build_transcode_url_errors(void) {
  pp_server srv = {"http://192.0.2.10:32400", "tok123", "cid"};
  char small[32];
  CHECK(pp_build_transcode_url(&srv, "/library/metadata/1", "s", 640, 480, 1500, 0,
                               small, sizeof small) == PP_ERR_ARG);
  CHECK(pp_build_transcode_url(NULL, "/library/metadata/1", "s", 640, 480, 1500, 0,
                               small, sizeof small) == PP_ERR_ARG);
}

/* ---------- transcode decision ---------- */

static void test_parse_decision_ok(void) {
  char *j = read_fixture("decision.json");
  /* directPlay disabled but conversion allowed: codes 1001/1001 */
  CHECK(pp_parse_decision(j) == PP_OK);
  free_fixture(j);
}

static void test_parse_decision_refused(void) {
  char *j = read_fixture("decision_refused.json");
  CHECK(pp_parse_decision(j) == PP_ERR_HTTP);
  free_fixture(j);
}

static void test_parse_decision_bad(void) {
  CHECK(pp_parse_decision("not json") == PP_ERR_PARSE);
  CHECK(pp_parse_decision(NULL) == PP_ERR_ARG);
  /* no decision codes at all: nothing refused */
  CHECK(pp_parse_decision("{\"MediaContainer\":{}}") == PP_OK);
}

static void test_build_decision_url_matches_start_params(void) {
  pp_server srv = {"http://192.0.2.10:32400", "tok123", "cid"};
  char dec[1024], start[1024];
  CHECK(pp_build_decision_url(&srv, "/library/metadata/53834", "sess-9",
                              640, 480, 1500, 0, dec, sizeof dec) == 0);
  CHECK(strstr(dec, "/video/:/transcode/universal/decision?") != NULL);
  CHECK(strstr(dec, "subtitles=burn") != NULL);
  CHECK(strstr(dec, "session=sess-9") != NULL);
  CHECK(strstr(dec, "X-Plex-Platform=Chrome") != NULL);
  CHECK(strstr(dec, "videoResolution=640x480") != NULL);
  CHECK(strstr(dec, "directPlay=0&directStream=0") != NULL);
  CHECK(strstr(dec, "path=/library/metadata/53834") != NULL);
  CHECK(strstr(dec, "X-Plex-Token=") == NULL); /* token goes in a header */
  char small[16];
  CHECK(pp_build_decision_url(&srv, "/library/metadata/53834", "s", 640, 480, 1500, 0,
                              small, sizeof small) == PP_ERR_ARG);
  CHECK(small[0] == '\0'); /* caller buffer untouched on error */
  /* start URL carries the same params plus the token */
  CHECK(pp_build_transcode_url(&srv, "/library/metadata/53834", "sess-9",
                               640, 480, 1500, 0, start, sizeof start) == 0);
  const char *dec_q = strchr(dec, '?'), *start_q = strchr(start, '?');
  CHECK(dec_q && start_q);
  size_t dec_len = strcspn(dec_q + 1, "&"); /* params up to session differ only by token */
  CHECK(strncmp(dec_q + 1, start_q + 1, dec_len) == 0);
}

int main(void) {
  RUN(test_parse_pin_created);
  RUN(test_parse_pin_authorized);
  RUN(test_parse_pin_bad_json);
  RUN(test_parse_servers);
  RUN(test_parse_servers_bad);
  RUN(test_parse_sections);
  RUN(test_parse_shows);
  RUN(test_parse_seasons);
  RUN(test_parse_episodes);
  RUN(test_parse_ondeck);
  RUN(test_parse_music_kinds);
  RUN(test_parse_single_movie);
  RUN(test_parse_items_bad);
  RUN(test_list_free_null_safe);
  RUN(test_build_transcode_url_offset_zero);
  RUN(test_build_transcode_url_offset_seconds);
  RUN(test_build_transcode_url_errors);
  RUN(test_parse_decision_ok);
  RUN(test_parse_decision_refused);
  RUN(test_parse_decision_bad);
  RUN(test_build_decision_url_matches_start_params);
  return TEST_RESULT();
}
