/* platform/suspend.c: sleep/wake detection helpers. See suspend.h. */
#include "platform/suspend.h"

int pp_wall_gap_is_resume(long prev_ms, long now_ms, long threshold_ms) {
  if (prev_ms <= 0) return 0;
  if (threshold_ms < 0) threshold_ms = 0;
  return now_ms - prev_ms > threshold_ms;
}
