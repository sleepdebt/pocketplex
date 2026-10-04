/* player/pp_play.c: device test harness for player_mpv.c. Not part of the app build.
 *
 *   pp_play [--sdl] <start_ms> <url-file>
 *
 * Reads the stream URL from a file (keeps the token out of argv and shell history), plays it through
 * player.h, and prints the position once a second until mpv exits (B/Menu on the SP). With --sdl it
 * first opens a fullscreen SDL window (blue), like the app would, and repaints it green after
 * player_stop, so you can check the screen is handed back. Build: see docs/devices.md.
 */
#include "player/player.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef PP_PLAY_SDL
#include <SDL.h>
static SDL_Window *win;
static SDL_Renderer *ren;
static void fill(int r, int g, int b) {
  SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255);
  SDL_RenderClear(ren);
  SDL_RenderPresent(ren);
}
#endif

int main(int argc, char **argv) {
  int use_sdl = 0, a = 1, fin = 0, rc = 0, n = 0;
  long start_ms, pos = 0;
  char url[4096];
  FILE *f;
  pp_player *p;
  time_t t0;

  if (a < argc && strcmp(argv[a], "--sdl") == 0) { use_sdl = 1; a++; }
  if (argc - a != 2) { fprintf(stderr, "usage: pp_play [--sdl] <start_ms> <url-file>\n"); return 2; }
  start_ms = atol(argv[a]);
  if (!(f = fopen(argv[a + 1], "r")) || !fgets(url, sizeof url, f)) { perror("url-file"); return 2; }
  fclose(f);
  url[strcspn(url, "\r\n")] = 0;
  if (getenv("PP_DEBUG")) pp_log_set_level(PP_LOG_DEBUG);

#ifdef PP_PLAY_SDL
  if (use_sdl) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) { LOGE("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_JoystickOpen(0);   /* like the app: our queue sees the same buttons the player reads */
    win = SDL_CreateWindow("pp_play", 0, 0, 640, 480, SDL_WINDOW_FULLSCREEN);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    if (!ren) { LOGE("SDL window: %s", SDL_GetError()); return 1; }
    fill(0, 0, 160);
    LOGI("pp_play: SDL window up (blue)");
    sleep(2);
  }
#else
  if (use_sdl) { fprintf(stderr, "built without PP_PLAY_SDL\n"); return 2; }
#endif

  t0 = time(NULL);
  if (!(p = player_start(url, start_ms))) { LOGE("pp_play: player_start failed"); return 1; }
  while (!fin) {
    sleep(1);
    rc = player_poll(p, &pos, &fin);
    printf("t=%lds pos=%ldms fin=%d rc=%d\n", (long)(time(NULL) - t0), pos, fin, rc);
    fflush(stdout);
    n++;
  }
  player_stop(p);
  printf("done: last pos=%ldms rc=%d polls=%d\n", pos, rc, n);

#ifdef PP_PLAY_SDL
  if (use_sdl) {
    SDL_Event ev;
    int stale = 0;
    while (SDL_PollEvent(&ev)) stale++;
    printf("sdl: %d stale events flushed after player_stop\n", stale);
    fill(0, 160, 0);
    LOGI("pp_play: screen handed back (green) for 5 s");
    sleep(5);
    SDL_Quit();
  }
#endif
  return rc < 0 ? 1 : 0;
}
