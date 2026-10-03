/* main.c: core stub that opens a 640x480 SDL2 window. core owns this file from Phase 1
 * and replaces it with the app loop on top of platform.h.
 *
 *   ./build/pocketplex [--exit-after-ms N]   (Esc or closing the window quits)
 */
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define SCREEN_W 640
#define SCREEN_H 480

int main(int argc, char **argv) {
  long exit_after_ms = -1;
  SDL_Window *win;
  SDL_Renderer *ren;
  Uint32 start;
  int running = 1, w = 0, h = 0, i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--exit-after-ms") == 0 && i + 1 < argc) exit_after_ms = atol(argv[++i]);
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    LOGE("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  win = SDL_CreateWindow("PocketPlex", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
  if (!win) {
    LOGE("SDL_CreateWindow: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) {
    LOGE("SDL_CreateRenderer: %s", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  SDL_GetWindowSize(win, &w, &h);
  LOGI("window open %dx%d (SDL %d.%d.%d, video driver %s)", w, h, SDL_MAJOR_VERSION,
       SDL_MINOR_VERSION, SDL_PATCHLEVEL, SDL_GetCurrentVideoDriver());

  start = SDL_GetTicks();
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = 0;
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
    }
    if (exit_after_ms >= 0 && (long)(SDL_GetTicks() - start) >= exit_after_ms) running = 0;

    SDL_SetRenderDrawColor(ren, 0x18, 0x18, 0x1c, 0xff);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }

  LOGI("exit");
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
