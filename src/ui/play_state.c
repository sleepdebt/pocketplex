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

int play_quality_kbps(const char *quality) {
  return (quality && strcmp(quality, "360p") == 0) ? 800 : 1500;
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
