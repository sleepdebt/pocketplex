/* tests/test_ui_suspend.c: wall-clock gap heuristic for sleep/wake. No SDL.
 * Sleep drops Wi-Fi on the handhelds; a forward wall-clock jump between two
 * main-loop ticks means the device slept and Wi-Fi is reconnecting. */
#include "test.h"
#include "platform/suspend.h"

static void test_gap_beyond_threshold_is_resume(void) {
  CHECK(pp_wall_gap_is_resume(100000, 110001, PP_RESUME_GAP_MS) == 1);
  CHECK(pp_wall_gap_is_resume(100000, 110000, PP_RESUME_GAP_MS) == 0);  /* exactly 10 s: no */
  CHECK(pp_wall_gap_is_resume(100000, 109999, PP_RESUME_GAP_MS) == 0);
  CHECK(pp_wall_gap_is_resume(0, 20000, PP_RESUME_GAP_MS) == 0);       /* no previous tick */
}

static void test_small_gaps_and_backwards_jumps(void) {
  CHECK(pp_wall_gap_is_resume(100000, 100016, PP_RESUME_GAP_MS) == 0);  /* one frame */
  CHECK(pp_wall_gap_is_resume(100000, 100000, PP_RESUME_GAP_MS) == 0);  /* same tick */
  CHECK(pp_wall_gap_is_resume(100000, 50000, PP_RESUME_GAP_MS) == 0);   /* clock set back */
  CHECK(pp_wall_gap_is_resume(0, 0, PP_RESUME_GAP_MS) == 0);
}

static void test_custom_threshold(void) {
  CHECK(pp_wall_gap_is_resume(0, 5001, 5000) == 0);   /* prev <= 0: never a resume */
  CHECK(pp_wall_gap_is_resume(1, 5002, 5000) == 1);
  CHECK(pp_wall_gap_is_resume(1, 5001, 5000) == 0);   /* strictly greater */
  CHECK(pp_wall_gap_is_resume(1, 101, 0) == 1);       /* any forward jump */
}

int main(void) {
  RUN(test_gap_beyond_threshold_is_resume);
  RUN(test_small_gaps_and_backwards_jumps);
  RUN(test_custom_threshold);
  return TEST_RESULT();
}
