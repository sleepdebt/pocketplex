/* player/player.h: playback contract.
 * Implemented by core (player_mpv.c / player_ffplay.c / player_embedded.c).
 * Change requests go to the spec.
 */
#ifndef PP_PLAYER_H
#define PP_PLAYER_H

typedef struct pp_player pp_player;

/* Hands the screen to the player and starts playing url at start_ms. NULL on failure. */
pp_player *player_start(const char *url, long start_ms);

/* Call about every 1 s. Writes the current position, and finished=1 once the player has
 * exited (end of media or user quit). 0 = ok, <0 = error (treat as finished). */
int  player_poll(pp_player*, long *pos_ms, int *finished);

/* Stops the player if still running, frees it, and gives the screen back to the UI. */
void player_stop(pp_player*);

#endif /* PP_PLAYER_H */
