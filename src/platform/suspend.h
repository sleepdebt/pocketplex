/* platform/suspend.h: sleep/wake detection helpers. Pure (no SDL), unit-tested.
 * Sleep drops Wi-Fi on the handhelds; the app treats a resume as "reconnect".
 * Monotonic clocks (SDL_GetTicks) freeze across suspend, so detection uses the
 * wall clock: a forward jump between two main-loop ticks means the device slept. */
#ifndef PP_SUSPEND_H
#define PP_SUSPEND_H

/* A forward wall-clock jump of more than this between two ticks is a resume. */
#define PP_RESUME_GAP_MS 10000L

/* 1 when now_ms jumped forward past prev_ms by more than threshold_ms.
 * prev_ms <= 0 (no tick recorded yet) and backwards jumps are not resumes. */
int pp_wall_gap_is_resume(long prev_ms, long now_ms, long threshold_ms);

#endif /* PP_SUSPEND_H */
