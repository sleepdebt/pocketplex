/* tests/test_ui_play_state.c: playback cadence/scrobble logic. No SDL, no mpv. */
#include <string.h>
#include "test.h"
#include "ui/play_state.h"

static void test_poll_cadence(void) {
  play_state s;
  play_state_init(&s, 60000, 0, 1000);
  CHECK(play_poll_due(&s, 1000));           /* first poll right away */
  play_update(&s, 0, 1000);
  CHECK(!play_poll_due(&s, 1500));
  CHECK(!play_poll_due(&s, 1999));
  CHECK(play_poll_due(&s, 2000));           /* about every 1 s */
}

static void test_timeline_every_10s(void) {
  play_state s;
  play_state_init(&s, 600000, 5000, 0);
  CHECK(play_update(&s, 5000, 0) & PLAY_ACT_TIMELINE);   /* immediately: keeps the transcode alive */
  int sent = 0;
  for (long t = 1000; t <= 30000; t += 1000)
    if (play_update(&s, 5000 + t, t) & PLAY_ACT_TIMELINE) sent++;
  CHECK(sent == 3);                          /* at 10 s, 20 s, 30 s */
  CHECK(s.pos_ms == 35000);
}

static void test_scrobble_once_at_90(void) {
  play_state s;
  play_state_init(&s, 100000, 0, 0);
  CHECK(!(play_update(&s, 89999, 0) & PLAY_ACT_SCROBBLE));
  CHECK(play_update(&s, 90000, 1000) & PLAY_ACT_SCROBBLE);
  CHECK(!(play_update(&s, 95000, 2000) & PLAY_ACT_SCROBBLE));  /* only once */
  CHECK(s.scrobbled);
}

static void test_no_scrobble_without_duration(void) {
  play_state s;
  play_state_init(&s, 0, 0, 0);
  CHECK(!(play_update(&s, 1000000, 0) & PLAY_ACT_SCROBBLE));
}

/* Interrupted stream: mpv "finishes" early -> no scrobble. */
static void test_early_finish_no_scrobble(void) {
  play_state s;
  play_state_init(&s, 1200000, 0, 0);
  play_update(&s, 120000, 0);
  CHECK(!s.scrobbled);
}

static void test_fmt_time(void) {
  char b[16];
  play_fmt_time(0, b, sizeof b);         CHECK_STR(b, "0:00");
  play_fmt_time(65000, b, sizeof b);     CHECK_STR(b, "1:05");
  play_fmt_time(1865674, b, sizeof b);   CHECK_STR(b, "31:05");
  play_fmt_time(3723000, b, sizeof b);   CHECK_STR(b, "1:02:03");
  play_fmt_time(-5, b, sizeof b);        CHECK_STR(b, "0:00");
}

static void test_quality_kbps(void) {
  CHECK(play_quality_kbps("480p") == 1500);
  CHECK(play_quality_kbps("360p") == 800);
  CHECK(play_quality_kbps("") == 1500);
  CHECK(play_quality_kbps(NULL) == 1500);
}

static void test_session_id(void) {
  char a[37], b[37];
  play_session_id(a);
  play_session_id(b);
  CHECK(strlen(a) == 36);
  CHECK(a[8] == '-' && a[13] == '-' && a[18] == '-' && a[23] == '-');
  CHECK(a[14] == '4');                     /* UUID v4 */
  CHECK(strcmp(a, b) != 0);
}

int main(void) {
  RUN(test_poll_cadence);
  RUN(test_timeline_every_10s);
  RUN(test_scrobble_once_at_90);
  RUN(test_no_scrobble_without_duration);
  RUN(test_early_finish_no_scrobble);
  RUN(test_fmt_time);
  RUN(test_quality_kbps);
  RUN(test_session_id);
  return TEST_RESULT();
}
