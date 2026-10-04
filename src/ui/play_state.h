/* ui/play_state.h: playback cadence and scrobble logic. Pure, no SDL.
 * The player screen calls play_update() after each player_poll() and acts on
 * the returned flags (send a timeline, scrobble). Times are monotonic ms.
 */
#ifndef PP_PLAY_STATE_H
#define PP_PLAY_STATE_H

#include <stddef.h>

#define PLAY_POLL_MS       1000   /* player_poll about every 1 s */
#define PLAY_TIMELINE_MS  10000   /* pp_timeline(playing) about every 10 s */
#define PLAY_SCROBBLE_PCT    90   /* mark watched at >= 90% */

enum { PLAY_ACT_TIMELINE = 1, PLAY_ACT_SCROBBLE = 2 };

typedef struct {
  long duration_ms;
  long pos_ms;
  long next_poll;
  long next_timeline;
  int  scrobbled;
} play_state;

void play_state_init(play_state *s, long duration_ms, long start_ms, long now_ms);
int  play_poll_due(const play_state *s, long now_ms);
/* Record a polled position; returns PLAY_ACT_* flags. */
int  play_update(play_state *s, long pos_ms, long now_ms);

/* "m:ss" or "h:mm:ss". */
void play_fmt_time(long ms, char *out, size_t n);
/* Max video bitrate for the Settings quality preset. */
int  play_quality_kbps(const char *quality);
/* Random UUID v4 for the PMS transcode session. */
void play_session_id(char out[37]);

#endif /* PP_PLAY_STATE_H */
