/* play_state.c: playback cadence and scrobble logic. See play_state.h. */
#include "ui/play_state.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

void play_state_init(play_state *s, long duration_ms, long start_ms, long now_ms) {
  memset(s, 0, sizeof(*s));
  s->duration_ms = duration_ms;
  s->pos_ms = start_ms;
  s->next_poll = now_ms;
  s->next_timeline = now_ms;  /* first timeline right away keeps the transcode alive */
}

int play_poll_due(const play_state *s, long now_ms) {
  return now_ms >= s->next_poll;
}

int play_update(play_state *s, long pos_ms, long now_ms) {
  int act = 0;
  s->pos_ms = pos_ms;
  s->next_poll = now_ms + PLAY_POLL_MS;
  if (now_ms >= s->next_timeline) {
    act |= PLAY_ACT_TIMELINE;
    s->next_timeline = now_ms + PLAY_TIMELINE_MS;
  }
  /* Position-based only, so a stream that "ends" early (PMS dropped it)
   * is never scrobbled. */
  if (!s->scrobbled && s->duration_ms > 0 &&
      pos_ms * 100 >= s->duration_ms * PLAY_SCROBBLE_PCT) {
    act |= PLAY_ACT_SCROBBLE;
    s->scrobbled = 1;
  }
  return act;
}

void play_fmt_time(long ms, char *out, size_t n) {
  long sec = ms > 0 ? ms / 1000 : 0;
  long h = sec / 3600, m = (sec / 60) % 60, ss = sec % 60;
  if (h > 0) snprintf(out, n, "%ld:%02ld:%02ld", h, m, ss);
  else snprintf(out, n, "%ld:%02ld", m, ss);
}

static const pp_quality g_presets[] = {
  { "480p", 640, 480, 1500 },   /* default; SP-verified soak profile */
  { "360p", 640, 480,  800 },   /* same canvas as 480p (4:3 fills it), lower bitrate */
  { "768p", 1024, 768, 3000 },  /* Brick Pro panel; PP_QUALITY_768P=1 until Phase 0 #7 */
};
#define N_PRESETS ((int)(sizeof g_presets / sizeof g_presets[0]))

int pp_quality_768p_enabled(void) {
  const char *e = getenv("PP_QUALITY_768P");
  return e && (strcmp(e, "1") == 0 || strcmp(e, "yes") == 0 || strcmp(e, "true") == 0);
}

static int preset_enabled(int i) {
  return i < N_PRESETS - 1 || pp_quality_768p_enabled();  /* 768p is last */
}

int pp_quality_get(const char *name, pp_quality *out) {
  for (int i = 0; i < N_PRESETS; i++) {
    if (name && strcmp(name, g_presets[i].name) == 0 && preset_enabled(i)) {
      if (out) *out = g_presets[i];
      return 0;
    }
  }
  if (out) *out = g_presets[0];
  return 1;
}

const char *pp_quality_next(const char *name) {
  pp_quality cur;
  pp_quality_get(name, &cur);
  for (int i = 0; i < N_PRESETS; i++) {
    if (strcmp(cur.name, g_presets[i].name) != 0) continue;
    for (int j = 1; j <= N_PRESETS; j++) {
      int k = (i + j) % N_PRESETS;
      if (preset_enabled(k)) return g_presets[k].name;
    }
  }
  return g_presets[0].name;
}

int play_quality_kbps(const char *quality) {
  pp_quality q;
  pp_quality_get(quality, &q);
  return q.kbps;
}

void play_session_id(char out[37]) {
  unsigned char b[16];
  int ok = 0;
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    ok = read(fd, b, sizeof b) == (ssize_t)sizeof b;
    close(fd);
  }
  if (!ok) {
    static unsigned seed;
    if (!seed) seed = (unsigned)time(NULL) ^ (unsigned)getpid();
    for (int i = 0; i < 16; i++) b[i] = (unsigned char)(rand_r(&seed) >> 7);
  }
  b[6] = (unsigned char)((b[6] & 0x0f) | 0x40);  /* version 4 */
  b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);  /* RFC 4122 variant */
  snprintf(out, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
           b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

int play_normalize_key(const char *in, char *out, size_t n) {
  if (!in || !in[0] || !out || n == 0) return -1;
  int w;
  if (in[0] == '/') {
    if (!in[1]) return -1;
    w = snprintf(out, n, "%s", in);
  } else {
    for (const char *p = in; *p; p++)
      if (*p < '0' || *p > '9') return -1;
    w = snprintf(out, n, "/library/metadata/%s", in);
  }
  return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

int play_burn_subtitles(const char *cfg_subtitles) {
  return !(cfg_subtitles && strcmp(cfg_subtitles, "off") == 0);
}

void play_redact_url(const char *url, char *out, size_t n) {
  static const char key[] = "X-Plex-Token=";
  if (!out || n == 0) return;
  out[0] = '\0';
  if (!url) return;
  const char *t = strstr(url, key);
  if (!t) { snprintf(out, n, "%s", url); return; }
  size_t head = (size_t)(t - url) + sizeof key - 1;
  if (head >= n) { snprintf(out, n, "%.*s", (int)(n - 1), url); return; }
  const char *rest = strchr(t, '&');
  snprintf(out, n, "%.*sREDACTED%s", (int)head, url, rest ? rest : "");
}
