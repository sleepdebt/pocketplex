/* fake_provider.c: fake Plex data matching the plex.h contract.
 * Replace each call with the real pp_* once core merges.
 * All lists returned are heap-allocated so pp_list_free can free them.
 */
#include "ui/fake_provider.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Servers ------------------------------------------------------------- */

int fake_servers(pp_server **out, int *count) {
  pp_server *srv = (pp_server *)malloc(sizeof(pp_server) * 2);
  if (!srv) return PP_ERR_NOMEM;
  srv[0].url = strdup("http://192.168.1.100:32400");
  srv[0].token = NULL;
  srv[0].client_id = strdup("pocketplex-device");
  srv[1].url = srv[1].token = srv[1].client_id = NULL;
  if (!srv[0].url || !srv[0].client_id) {
    free(srv[0].url);
    free(srv[0].client_id);
    free(srv);
    return PP_ERR_NOMEM;
  }
  *out = srv;
  *count = 1;
  return 0;
}

/* ---- Sections (library roots) -------------------------------------------- */

static pp_item make_section(int idx, const char *title, pp_kind kind, const char *key) {
  pp_item it;
  (void)idx;
  memset(&it, 0, sizeof(it));
  snprintf(it.key, sizeof(it.key), "%s", key);
  snprintf(it.title, sizeof(it.title), "%s", title);
  it.kind = kind;
  it.leaf_count = 0;
  it.viewed_leaf_count = 0;
  return it;
}

int fake_sections(const pp_server *srv, pp_list *out) {
  if (!out) return PP_ERR_ARG;
  (void)srv;
  pp_item *items = (pp_item *)malloc(sizeof(pp_item) * 5);
  if (!items) return PP_ERR_NOMEM;
  items[0] = make_section(0, "Movies", PP_SECTION, "movies");
  items[1] = make_section(1, "TV Shows", PP_SECTION, "tv");
  items[2] = make_section(2, "Music", PP_SECTION, "music");
  items[3] = make_section(3, "Recently Added", PP_SECTION, "recents");
  items[4] = make_section(4, "Continue Watching", PP_SECTION, "continue");
  out->items = items;
  out->count = 5;
  return 0;
}

/* ---- Children (library contents) ----------------------------------------- */

static pp_item make_episode(const char *show_title, int season, int ep, long dur) {
  pp_item it;
  memset(&it, 0, sizeof(it));
  snprintf(it.rating_key, sizeof(it.rating_key), "ep_%d_%d", season, ep);
  snprintf(it.key, sizeof(it.key), "/library/metadata/%d.%d", season * 100 + ep, 0);
  snprintf(it.title, sizeof(it.title), "%s - S%02dE%02d", show_title, season, ep);
  snprintf(it.subtitle, sizeof(it.subtitle), "Season %d, Episode %d", season, ep);
  it.kind = PP_EPISODE;
  it.duration_ms = dur;
  it.view_offset_ms = 0;
  it.watched = (ep < 3) ? 1 : 0;
  it.year = 2024;
  snprintf(it.summary, sizeof(it.summary),
           "An episode from %s. Season %d, episode %d of the series.", show_title, season, ep);
  return it;
}

static pp_item make_movie(const char *title, int year, const char *genre) {
  pp_item it;
  memset(&it, 0, sizeof(it));
  snprintf(it.rating_key, sizeof(it.rating_key), "movie_%s", title);
  snprintf(it.key, sizeof(it.key), "/library/metadata/movie_%s", title);
  snprintf(it.title, sizeof(it.title), "%s", title);
  snprintf(it.subtitle, sizeof(it.subtitle), "%d \xc2\xb7 %s", year, genre);
  it.kind = PP_MOVIE;
  it.year = year;
  it.duration_ms = 7200000;
  it.view_offset_ms = 0;
  it.watched = 0;
  snprintf(it.summary, sizeof(it.summary),
           "A gripping tale of %s. Rated for mature audiences.", title);
  return it;
}

int fake_children(const pp_server *srv, const char *key, pp_list *out) {
  (void)srv;
  if (!key || !out) return PP_ERR_ARG;
  if (strcmp(key, "movies") == 0) {
    static const char *titles[] = {
      "Neon Horizon", "Silent Depths", "Crimson Tide", "Stoneheart",
      "The Last Signal", "Echo Chamber", "Glass Horizon", "Velocity",
      "The Forgotten", "Black Moon", "White Noise", "Red Shift",
      "Frozen Sky", "Broken Circuit", "Ghost Protocol", "Solar Flare",
      "The Edge", "Vanishing Point", "Iron Gate", "Deep Impact",
      "The Call", "Dark Waters", "Lost Signal", "The Departure",
      "Final Hour", "The Reckoning", "Ashen Ground", "Shattered Light",
      "The Crossing", "Midnight Run", "Steel Heart", "The Breach",
      "Echo Valley", "The Stand", "Northern Lights", "The Signal",
      "Gravity", "The Shift", "Momentum", "The Collapse",
      "Aftermath", "The Divide", "Threshold", "The Pulse",
      "Zero Hour", "The Rift", "Blackout", "The Silence",
      "Daylight", "The Return"
    };
    int n = (int)(sizeof(titles)/sizeof(titles[0]));
    pp_item *items = (pp_item *)malloc(sizeof(pp_item) * n);
    if (!items) return PP_ERR_NOMEM;
    for (int i = 0; i < n; i++)
      items[i] = make_movie(titles[i], 2020 + (i % 5), "Drama");
    out->items = items;
    out->count = n;
    return 0;
  }
  if (strcmp(key, "tv") == 0) {
    static const char *shows[] = {
      "The Last Stand", "Echo Point", "Nexus", "The Deep", "Solar Winds",
      "Quantum Leap", "Shadow Fall", "Iron Will", "The Long Road", "Arcadia"
    };
    int idx = 0;
    int cap = 2000;
    pp_item *items = (pp_item *)malloc(sizeof(pp_item) * cap);
    if (!items) return PP_ERR_NOMEM;
    for (int s = 0; s < 10 && idx < 2000; s++) {
      int eps = (s == 0 || s == 1) ? 300 : 200;
      for (int ep = 1; ep <= eps && idx < 2000; ep++) {
        items[idx] = make_episode(shows[s], 1, ep, 1800000);
        idx++;
      }
    }
    out->items = items;
    out->count = idx;
    return 0;
  }
  out->items = NULL;
  out->count = 0;
  return PP_ERR_ARG;
}

/* ---- On-deck / Continue Watching ----------------------------------------- */

int fake_on_deck(const pp_server *srv, pp_list *out) {
  (void)srv;
  if (!out) return PP_ERR_ARG;
  pp_item *items = (pp_item *)malloc(sizeof(pp_item) * 6);
  if (!items) return PP_ERR_NOMEM;
  items[0] = make_episode("The Last Stand", 1, 5, 1800000);
  items[0].view_offset_ms = 900000;
  items[1] = make_episode("Echo Point", 2, 3, 1800000);
  items[1].view_offset_ms = 600000;
  items[2] = make_movie("Neon Horizon", 2023, "Sci-Fi");
  items[2].view_offset_ms = 3600000;
  items[3] = make_episode("Nexus", 1, 8, 1800000);
  items[3].view_offset_ms = 1200000;
  items[4] = make_episode("Shadow Fall", 3, 2, 1800000);
  items[5] = make_movie("Velocity", 2022, "Action");
  out->items = items;
  out->count = 6;
  return 0;
}

/* ---- Single item (detail screen) ----------------------------------------- */

const pp_item *fake_item(const pp_server *srv, const char *rating_key) {
  (void)srv;
  if (!rating_key) return NULL;
  static pp_item detail_item;
  memset(&detail_item, 0, sizeof(detail_item));
  if (strncmp(rating_key, "movie_", 6) == 0) {
    detail_item = make_movie(rating_key + 6, 2023, "Sci-Fi");
    detail_item.view_offset_ms = 3600000;
  } else {
    detail_item = make_episode("The Last Stand", 1, 5, 1800000);
    detail_item.view_offset_ms = 900000;
  }
  return &detail_item;
}

/* ---- Settings ------------------------------------------------------------ */

pp_settings *fake_settings(void) {
  static pp_settings s = { 1, 1, 1 };
  return &s;
}
