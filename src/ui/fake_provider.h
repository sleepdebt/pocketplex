/* ui/fake_provider.h: fake Plex data for desktop dev until core merges.
 * Implements the same signatures as plex.h so the screens swap over trivially.
 */
#ifndef PP_FAKE_PROVIDER_H
#define PP_FAKE_PROVIDER_H

#include "plex/plex.h"

/* Returns 0 fake servers. Caller frees with pp_servers_free. */
int fake_servers(pp_server **out, int *count);

/* Fake library sections for a server. Fills an existing pp_list. */
int fake_sections(const pp_server *srv, pp_list *out);

/* Fake children under a section key. Fills an existing pp_list.
 * Keys: "movies" → 50 movies, "tv" → 2000 episodes across 10 shows. */
int fake_children(const pp_server *srv, const char *key, pp_list *out);

/* Fake on-deck / continue watching. */
int fake_on_deck(const pp_server *srv, pp_list *out);

/* Returns a single item by rating key (for detail screen). Caller frees via pp_list_free. */
const pp_item *fake_item(const pp_server *srv, const char *rating_key);

/* Fake settings for the settings screen. */
typedef struct {
  int quality_480p;   /* 1 = 480p, 0 = 360p */
  int subtitles;      /* 1 = burn, 0 = off */
  int signed_in;
} pp_settings;

pp_settings *fake_settings(void);

#endif /* PP_FAKE_PROVIDER_H */
