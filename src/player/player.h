/* player/player.h: playback contract.
 * Implemented by core (player_mpv.c / player_ffplay.c / player_embedded.c).
 * Change requests go to the spec.
 */
#ifndef PP_PLAYER_H
#define PP_PLAYER_H

typedef struct pp_player pp_player;

/* Audio delay in ms for the next player_start (Brick Pro). + makes the sound later. */
void player_set_audio_delay_ms(int ms);

/* Hands the screen to the player and starts playing url at start_ms. NULL on failure. */
pp_player *player_start(const char *url, long start_ms);

/* Call about every 1 s. Writes the current position, and finished=1 once the player has
 * exited (end of media or user quit). 0 = ok, <0 = error (treat as finished). */
int  player_poll(pp_player*, long *pos_ms, int *finished);

/* Stops the player if still running, frees it, and gives the screen back to the UI. */
void player_stop(pp_player*);

/* Renders one video frame into the player's surface (the Brick Pro libmpv
 * render-API path draws into the app's GL context), then swaps once. Safe to
 * call every frame from the screen render loop; a no-op for the
 * external-process players, so SP/MMP/desktop are unchanged. */
int  player_render_frame(pp_player*);   /* 1 when a frame was presented this call */
void player_draw_loader(unsigned tick); /* tg5040: loading bar until the first frame */

#endif /* PP_PLAYER_H */
