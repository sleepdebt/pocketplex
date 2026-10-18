/* platform/evdev_tg5040.h: internal raw-evdev input backend for tg5040.
 *
 * SDL 2.26 in the TrimUI SDK does not enumerate the built-in "TRIMUI Player1"
 * pad, so sdl2.c delegates tg5040 input to this reader (decision:
 * ops/reviews/tg-0-input.md). Internal to the platform layer: NOT part of the
 * platform.h contract. Only compiled for PLATFORM=tg5040 (needs linux/input.h).
 */
#ifndef PP_EVDEV_TG5040_H
#define PP_EVDEV_TG5040_H

#include "platform/platform.h"

/* Find and open the pad: first /dev/input/event* whose EV_KEY capability
 * bitmap has BTN_SOUTH (the has_gamepad_keys gate player_mpv.c uses; on the
 * Brick Pro that is TRIMUI Player1, event3). 0 = ok, -1 = none found. */
int pp_evdev_tg5040_open(void);

/* Non-blocking: drain pending events and return the next queued button press
 * (BTN_NONE when nothing was pressed). Presses are edge-triggered; device key
 * auto-repeat (value 2) is ignored in favour of the UI's own repeat. */
pp_btn pp_evdev_tg5040_poll(void);

/* The D-pad/L1/R1 button still held down, for the shared UI repeat logic.
 * BTN_NONE once released (or when the held input's source state changed). */
pp_btn pp_evdev_tg5040_held(void);

void pp_evdev_tg5040_close(void);

#endif /* PP_EVDEV_TG5040_H */
