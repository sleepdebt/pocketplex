/* config/config.h: pocketplex.ini reader/writer.
 *
 * Plain INI next to the binary; see pocketplex.ini.example. A missing file
 * loads as defaults (0 is returned), so first run works without a config.
 * All string fields are fixed-size and always NUL-terminated; over-long
 * values in the file are truncated, never overflowed.
 */
#ifndef PP_CONFIG_H
#define PP_CONFIG_H

typedef struct {
  char server_url[256]; /* e.g. http://192.168.1.10:32400 */
  char token[256];      /* X-Plex-Token; empty = not logged in */
  char client_id[64];   /* X-Plex-Client-Identifier, generated once */
  char quality[16];     /* "360p" | "480p" */
  char subtitles[16];   /* "burn" | "off" */
  int  swap_ab;         /* A/B button swap */
} pp_config;

void pp_config_defaults(pp_config *cfg);
/* Loads from path; 0 = ok. A missing file loads defaults. Junk lines are
 * skipped, so no parse error is reported: load always returns 0 unless the
 * file exists but can't be read (then <0). */
int  pp_config_load(pp_config *cfg, const char *path);
/* Writes the canonical file (0600 - it holds the account token) via a .tmp
 * rename. 0 = ok, <0 = couldn't write. */
int  pp_config_save(const pp_config *cfg, const char *path);
/* Generates a random client id into cfg->client_id if it is empty. */
void pp_config_ensure_client_id(pp_config *cfg);

#endif /* PP_CONFIG_H */
