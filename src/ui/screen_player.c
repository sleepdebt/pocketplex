/* screen_player.c: plays one item through the external player.
 * Follows core's integration rules (the spec):
 *   1. pp_transcode_url (offset 0) on the worker, then player_start(url, start_ms).
 *   2. While playing: no render / plat_present; player_poll about every 1 s;
 *      pp_timeline(playing) about every 10 s; scrobble at >= 90%.
 *   3. On exit: draw a "Stopping..." frame, then player_stop (blocks ~1.5 s),
 *      pp_timeline(stopped) + pp_transcode_stop, SDL_FlushEvents, redraw.
 */
#include "ui.h"
#include "ui/worker.h"
#include "ui/play_state.h"
#include "player/player.h"
#include "config/config.h"
#include "log.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { P_RESOLVE, P_PLAYING, P_STOP_DRAW, P_STOPPING } player_phase;

typedef struct {
  pp_item item;
  long start_ms;
  char session[37];
  int kbps;
  player_phase phase;
  pp_request *req_url;   /* owned reference while resolving */
  pp_player *player;     /* non-NULL between player_start and player_stop */
  play_state ps;
  Uint32 play_started;
  int failed;            /* player_poll < 0 */
} player_data_t;

static pp_play_args play_args(player_data_t *d, const char *state) {
  pp_play_args a = { &d->item, d->session, state, d->ps.pos_ms, d->kbps };
  return a;
}

/* Fire-and-forget: released immediately, the worker frees it when done. */
static void send_bg(player_data_t *d, pp_req_type type, const char *state) {
  pp_play_args a = play_args(d, state);
  worker_release(worker_start_play(type, ui_current_server(), &a, 0));
}

static void begin_stop(pp_screen *self, player_data_t *d) {
  self->no_present = 0;   /* next frame draws "Stopping..." */
  d->phase = P_STOP_DRAW;
}

/* Stop the player and close out the PMS session. Safe to call twice. */
static void finish_playback(player_data_t *d) {
  if (!d->player) return;
  player_stop(d->player);
  d->player = NULL;
  send_bg(d, REQ_PLAY_STOP, "stopped");
  char pos[16];
  play_fmt_time(d->ps.pos_ms, pos, sizeof pos);
  LOGI("play: stopped %s at %s (%ld ms)%s", d->item.rating_key, pos, d->ps.pos_ms,
       d->failed ? " after player error" : "");
}

static void draw_status(const char *msg, int spinner) {
  ui_fill_rect(0, 0, PP_SCREEN_W, PP_SCREEN_H, PP_COLOR_BG);
  ui_draw_text(msg, PP_MARGIN_L, PP_SCREEN_H / 2 - 30, PP_COLOR_FG);
  if (spinner) {
    ui_draw_spinner(PP_SCREEN_W / 2 - 40, PP_SCREEN_H / 2 + 10, g_spinner_frame);
    g_spinner_frame++;
  }
}

static void player_render(pp_screen *self) {
  player_data_t *d = (player_data_t *)self->data;
  Uint32 now = SDL_GetTicks();

  switch (d->phase) {
  case P_RESOLVE: {
    if (!worker_is_done(d->req_url)) {
      draw_status("Starting playback...", 1);
      return;
    }
    int st = worker_status(d->req_url);
    if (st != PP_OK) {
      ui_toast(st == PP_ERR_AUTH ? "Auth expired — relink" : worker_error(d->req_url));
      worker_release(d->req_url);
      d->req_url = NULL;
      ui_pop();
      return;
    }
    /* From player_start until player_stop the UI must not present. */
    self->no_present = 1;
    d->player = player_start(worker_url(d->req_url), d->start_ms);
    worker_release(d->req_url);  /* wipes the tokenised URL */
    d->req_url = NULL;
    if (!d->player) {
      self->no_present = 0;
      ui_toast("Could not start the player");
      ui_pop();
      return;
    }
    play_state_init(&d->ps, d->item.duration_ms, d->start_ms, (long)now);
    d->play_started = now;
    d->phase = P_PLAYING;
    LOGI("play: started %s at %ld ms", d->item.rating_key, d->start_ms);
    return;
  }

  case P_PLAYING: {
    if (!play_poll_due(&d->ps, (long)now)) return;
    long pos = d->ps.pos_ms;
    int finished = 0;
    int rc = player_poll(d->player, &pos, &finished);
    if (rc < 0) {
      d->failed = 1;
      begin_stop(self, d);
      return;
    }
    int act = play_update(&d->ps, pos, (long)SDL_GetTicks());
    if (act & PLAY_ACT_TIMELINE) send_bg(d, REQ_TIMELINE, "playing");
    if (act & PLAY_ACT_SCROBBLE) {
      LOGI("play: scrobble %s at %ld ms", d->item.rating_key, pos);
      send_bg(d, REQ_SCROBBLE, NULL);
    }
    long smoke_ms = ui_smoke_play_ms();
    if (finished || (smoke_ms > 0 && (long)(now - d->play_started) >= smoke_ms))
      begin_stop(self, d);
    return;
  }

  case P_STOP_DRAW:
    /* Presented before player_stop blocks. */
    draw_status("Stopping...", 0);
    d->phase = P_STOPPING;
    return;

  case P_STOPPING:
    draw_status("Stopping...", 0);
    finish_playback(d);
    /* The player read the buttons itself; drop what SDL queued meanwhile. */
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    if (d->failed) ui_toast("Playback failed");
    ui_pop();
    return;
  }
}

static void player_handle(pp_screen *self, pp_btn btn) {
  player_data_t *d = (player_data_t *)self->data;
  /* While playing, the player owns input. B cancels while resolving. */
  if (d->phase == P_RESOLVE && btn == BTN_B) ui_pop();
}

static void player_destroy(pp_screen *self) {
  player_data_t *d = (player_data_t *)self->data;
  if (d) {
    finish_playback(d);          /* e.g. window closed mid-playback */
    worker_release(d->req_url);
    free(d);
  }
}

pp_screen *screen_player_create(const pp_item *item, long start_ms) {
  if (!item) return NULL;
  pp_screen *s = (pp_screen *)calloc(1, sizeof(pp_screen));
  if (!s) return NULL;
  player_data_t *d = (player_data_t *)calloc(1, sizeof(player_data_t));
  if (!d) { free(s); return NULL; }
  d->item = *item;
  d->start_ms = start_ms > 0 ? start_ms : 0;
  play_session_id(d->session);

  pp_config cfg;
  pp_config_defaults(&cfg);
  pp_config_load(&cfg, ui_ini_path());
  d->kbps = play_quality_kbps(cfg.quality);
  memset(&cfg, 0, sizeof cfg);   /* holds the token */

  s->id = SCREEN_PLAYER;
  s->data = d;
  s->loading = 1;
  s->render = player_render;
  s->handle_button = player_handle;
  s->destroy = player_destroy;

  pp_play_args a = play_args(d, NULL);
  d->req_url = worker_start_play(REQ_TRANSCODE_URL, ui_current_server(), &a,
                                 ui_is_smoke_scroll());
  return s;
}
