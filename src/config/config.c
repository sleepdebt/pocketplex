/* config/config.c: pocketplex.ini reader/writer. See config.h. */
#include "config/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void copy_field(char *dst, size_t n, const char *src) {
  if (!src) src = "";
  snprintf(dst, n, "%s", src); /* truncates, always NUL-terminates */
}

void pp_config_defaults(pp_config *cfg) {
  memset(cfg, 0, sizeof *cfg);
  copy_field(cfg->quality, sizeof cfg->quality, "480p");
  copy_field(cfg->subtitles, sizeof cfg->subtitles, "burn");
}

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = '\0';
  return s;
}

/* Strips an inline comment (" ;..." / " #..." with whitespace before it), surrounding
 * whitespace and matching quotes, in place. */
static void clean_value(char *v) {
  for (char *p = v; *p; p++) {
    if ((*p == ';' || *p == '#') && p > v && (p[-1] == ' ' || p[-1] == '\t')) { *p = '\0'; break; }
  }
  char *t = trim(v);
  if (t != v) memmove(v, t, strlen(t) + 1);
  size_t len = strlen(v);
  if (len >= 2 && ((v[0] == '"' && v[len - 1] == '"') || (v[0] == '\'' && v[len - 1] == '\''))) {
    v[len - 1] = '\0';
    memmove(v, v + 1, len - 1);
  }
}

static void assign(pp_config *cfg, const char *section, const char *key, char *value) {
  clean_value(value);
  if (strcmp(section, "plex") == 0) {
    if (strcmp(key, "server_url") == 0) copy_field(cfg->server_url, sizeof cfg->server_url, value);
    else if (strcmp(key, "token") == 0) copy_field(cfg->token, sizeof cfg->token, value);
    else if (strcmp(key, "client_id") == 0) copy_field(cfg->client_id, sizeof cfg->client_id, value);
  } else if (strcmp(section, "ui") == 0) {
    if (strcmp(key, "quality") == 0) copy_field(cfg->quality, sizeof cfg->quality, value);
    else if (strcmp(key, "subtitles") == 0) copy_field(cfg->subtitles, sizeof cfg->subtitles, value);
    else if (strcmp(key, "swap_ab") == 0)
      cfg->swap_ab = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcmp(value, "yes") == 0);
  }
}

int pp_config_load(pp_config *cfg, const char *path) {
  pp_config_defaults(cfg);
  FILE *f = fopen(path, "r");
  if (!f) return 0; /* missing file = defaults */

  char line[1024], section[64] = "";
  while (fgets(line, sizeof line, f)) {
    char *s = trim(line);
    if (*s == '\0' || *s == ';' || *s == '#') continue;
    if (*s == '[') {
      char *close = strchr(s, ']');
      if (!close) continue; /* junk line: skip */
      *close = '\0';
      copy_field(section, sizeof section, trim(s + 1));
      continue;
    }
    char *eq = strchr(s, '=');
    if (!eq) continue; /* junk or continuation of a long line: skip */
    *eq = '\0';
    char *key = trim(s), *value = eq + 1;
    assign(cfg, section, key, value);
  }
  fclose(f);
  return 0;
}

int pp_config_save(const pp_config *cfg, const char *path) {
  char tmp[512];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if (!f) return -1;
  fprintf(f,
          "# PocketPlex config. Copy to pocketplex.ini next to the binary.\n"
          "# Dev shortcut: server_url + token set = PIN login and discovery are skipped.\n"
          "\n"
          "[plex]\n"
          "server_url = %s\n"
          "token = %s\n"
          "client_id = %s\n"
          "\n"
          "[ui]\n"
          "quality = %s        ; 360p | 480p\n"
          "subtitles = %s      ; burn | off\n"
          "swap_ab = %s\n",
          cfg->server_url, cfg->token, cfg->client_id, cfg->quality, cfg->subtitles,
          cfg->swap_ab ? "true" : "false");
  if (fclose(f) != 0) return -1;
  if (rename(tmp, path) != 0) return -1;
  return 0;
}

void pp_config_ensure_client_id(pp_config *cfg) {
  if (cfg->client_id[0]) return;
  unsigned long a = (unsigned long)time(NULL) ^ (unsigned long)(uintptr_t)cfg;
  srandom((unsigned)(a ^ (a >> 32)));
  snprintf(cfg->client_id, sizeof cfg->client_id, "pp-%08lx-%08lx-%08lx",
           (unsigned long)random(), (unsigned long)random(), (unsigned long)random());
}
