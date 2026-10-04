#include "config/config.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char tmpdir[256];

static void write_ini(const char *content) {
  char path[512];
  snprintf(path, sizeof path, "%s/pp_test_config.ini", tmpdir);
  FILE *f = fopen(path, "w");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
  fputs(content, f);
  fclose(f);
}

static int load(pp_config *cfg) {
  char path[512];
  snprintf(path, sizeof path, "%s/pp_test_config.ini", tmpdir);
  return pp_config_load(cfg, path);
}

static void test_loads_all_fields(void) {
  write_ini(
      "# comment line\n"
      "[plex]\n"
      "server_url = http://192.0.2.10:32400\n"
      "token = abc123xyz\n"
      "client_id = my-uuid-1\n"
      "\n"
      "[ui]\n"
      "quality = 360p        ; comment after value\n"
      "subtitles = off\n"
      "swap_ab = true\n");
  pp_config cfg;
  CHECK(load(&cfg) == 0);
  CHECK_STR(cfg.server_url, "http://192.0.2.10:32400");
  CHECK_STR(cfg.token, "abc123xyz");
  CHECK_STR(cfg.client_id, "my-uuid-1");
  CHECK_STR(cfg.quality, "360p");
  CHECK_STR(cfg.subtitles, "off");
  CHECK(cfg.swap_ab == 1);
}

static void test_missing_file_gives_defaults(void) {
  pp_config cfg;
  char path[512];
  snprintf(path, sizeof path, "%s/does_not_exist.ini", tmpdir);
  CHECK(pp_config_load(&cfg, path) == 0);
  CHECK(cfg.server_url[0] == '\0');
  CHECK(cfg.token[0] == '\0');
  CHECK_STR(cfg.quality, "480p");
  CHECK_STR(cfg.subtitles, "burn");
  CHECK(cfg.swap_ab == 0);
}

static void test_quoted_values_and_spaces(void) {
  write_ini(
      "[plex]\n"
      "server_url=http://192.0.2.2:32400\n"
      "token   =   spaced-token   \n"
      "[ui]\n"
      "quality =\"480p\"\n");
  pp_config cfg;
  CHECK(load(&cfg) == 0);
  CHECK_STR(cfg.server_url, "http://192.0.2.2:32400");
  CHECK_STR(cfg.token, "spaced-token");
  CHECK_STR(cfg.quality, "480p");
}

static void test_save_roundtrip(void) {
  pp_config cfg;
  pp_config_defaults(&cfg);
  snprintf(cfg.server_url, sizeof cfg.server_url, "http://192.0.2.3:32400");
  snprintf(cfg.token, sizeof cfg.token, "tok-roundtrip");
  snprintf(cfg.client_id, sizeof cfg.client_id, "cid-42");
  snprintf(cfg.quality, sizeof cfg.quality, "360p");
  cfg.swap_ab = 1;
  char path[512];
  snprintf(path, sizeof path, "%s/pp_test_config.ini", tmpdir);
  CHECK(pp_config_save(&cfg, path) == 0);

  pp_config back;
  CHECK(pp_config_load(&back, path) == 0);
  CHECK_STR(back.server_url, "http://192.0.2.3:32400");
  CHECK_STR(back.token, "tok-roundtrip");
  CHECK_STR(back.client_id, "cid-42");
  CHECK_STR(back.quality, "360p");
  CHECK_STR(back.subtitles, "burn");
  CHECK(back.swap_ab == 1);
}

static void test_saved_file_has_no_secrets_leak_of_other_keys(void) {
  /* save() must not carry over stale values from a previous file */
  pp_config cfg;
  pp_config_defaults(&cfg);
  snprintf(cfg.token, sizeof cfg.token, "new-token");
  char path[512];
  snprintf(path, sizeof path, "%s/pp_test_config.ini", tmpdir);
  CHECK(pp_config_save(&cfg, path) == 0);
  pp_config back;
  CHECK(pp_config_load(&back, path) == 0);
  CHECK(back.server_url[0] == '\0'); /* was never set */
  CHECK_STR(back.token, "new-token");
}

static void test_client_id_generated_once_and_stable(void) {
  pp_config cfg;
  pp_config_defaults(&cfg);
  CHECK(cfg.client_id[0] == '\0');
  pp_config_ensure_client_id(&cfg);
  CHECK(cfg.client_id[0] != '\0');
  char first[64];
  snprintf(first, sizeof first, "%s", cfg.client_id);
  pp_config_ensure_client_id(&cfg); /* already set: unchanged */
  CHECK_STR(cfg.client_id, first);
  CHECK(strlen(cfg.client_id) >= 16); /* uuid-ish, not trivially short */
}

static void test_client_id_format(void) {
  pp_config a, b;
  pp_config_defaults(&a);
  pp_config_defaults(&b);
  pp_config_ensure_client_id(&a);
  pp_config_ensure_client_id(&b);
  /* pinned format: pp-<8 hex>-<8 hex>-<8 hex>, header/URL-safe chars only */
  CHECK(strlen(a.client_id) == 29);
  CHECK(strncmp(a.client_id, "pp-", 3) == 0);
  for (const char *p = a.client_id + 3; *p; p++)
    CHECK((*p == '-') || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'));
  CHECK(strcmp(a.client_id, b.client_id) != 0); /* different cfgs differ */
}

static void test_long_values_are_truncated_not_overflowed(void) {
  char big[2048];
  memset(big, 'x', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  char content[4096];
  snprintf(content, sizeof content, "[plex]\nserver_url = %s\ntoken = %s\n", big, big);
  write_ini(content);
  pp_config cfg;
  CHECK(load(&cfg) == 0); /* must not crash; fields truncated */
  CHECK(strlen(cfg.server_url) < sizeof cfg.server_url);
  CHECK(strlen(cfg.token) < sizeof cfg.token);
}

static void test_save_creates_private_file_no_tmp_left(void) {
  pp_config cfg;
  pp_config_defaults(&cfg);
  char path[512], tmppath[520];
  snprintf(path, sizeof path, "%s/pp_test_perm.ini", tmpdir);
  snprintf(tmppath, sizeof tmppath, "%s.tmp", path);
  remove(path);
  remove(tmppath);
  CHECK(pp_config_save(&cfg, path) == 0);
  struct stat st;
  CHECK(stat(path, &st) == 0);
  CHECK((st.st_mode & 0777) == 0600); /* holds the account token */
  CHECK(stat(tmppath, &st) != 0); /* no .tmp left behind */
  remove(path);
}

int main(void) {
  const char *t = getenv("TMPDIR");
  snprintf(tmpdir, sizeof tmpdir, "%s", (t && *t) ? t : "/tmp");
  RUN(test_loads_all_fields);
  RUN(test_missing_file_gives_defaults);
  RUN(test_quoted_values_and_spaces);
  RUN(test_save_roundtrip);
  RUN(test_saved_file_has_no_secrets_leak_of_other_keys);
  RUN(test_client_id_generated_once_and_stable);
  RUN(test_client_id_format);
  RUN(test_long_values_are_truncated_not_overflowed);
  RUN(test_save_creates_private_file_no_tmp_left);
  return TEST_RESULT();
}
