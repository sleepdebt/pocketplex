/* platform/sdl2.c: SDL2 backend for desktop and RG35XX SP.
 * Implements the frozen platform.h contract. Also exposes plat_renderer()
 * so the UI layer can draw with SDL_Renderer directly.
 */
#include "platform/platform.h"
#include "ui/input_map.h"
#include "log.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static int g_quit_requested = 0;
static void (*g_on_resume)(void) = NULL;

/* Return the renderer for the UI drawing layer. NULL until plat_init. */
SDL_Renderer *plat_renderer(void) { return g_ren; }
SDL_Window *plat_window(void) { return g_win; }

int plat_init(int *w, int *h) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
    LOGE("SDL_Init: %s", SDL_GetError());
    return -1;
  }
  if (TTF_Init() != 0) {
    LOGE("TTF_Init: %s", TTF_GetError());
    SDL_Quit();
    return -1;
  }

  int ww = 640, hh = 480;

  g_win = SDL_CreateWindow("PocketPlex",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           ww, hh, SDL_WINDOW_SHOWN);
  if (!g_win) {
    LOGE("SDL_CreateWindow: %s", SDL_GetError());
    TTF_Quit();
    SDL_Quit();
    return -1;
  }

  g_ren = SDL_CreateRenderer(g_win, -1,
                             SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
  if (!g_ren) {
    LOGE("SDL_CreateRenderer: %s", SDL_GetError());
    SDL_DestroyWindow(g_win);
    TTF_Quit();
    SDL_Quit();
    return -1;
  }

  if (w) *w = ww;
  if (h) *h = hh;
  g_quit_requested = 0;
  LOGI("platform ready: window %dx%d", ww, hh);
  return 0;
}

pp_btn plat_poll_button(void) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_QUIT:
      g_quit_requested = 1;
      return BTN_MENU;
    case SDL_KEYDOWN:
      if (ev.key.repeat) return BTN_NONE;
      return pp_keycode_to_btn(ev.key.keysym.sym);
    case SDL_CONTROLLERDEVICEADDED:
      SDL_GameControllerOpen(ev.cdevice.which);
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      /* Map common gamepad buttons to pp_btn. */
      switch (ev.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return BTN_DOWN;
      case SDL_CONTROLLER_BUTTON_DPAD_UP:    return BTN_UP;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return BTN_LEFT;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return BTN_RIGHT;
      case SDL_CONTROLLER_BUTTON_A:          return BTN_A;
      case SDL_CONTROLLER_BUTTON_B:          return BTN_B;
      case SDL_CONTROLLER_BUTTON_X:          return BTN_X;
      case SDL_CONTROLLER_BUTTON_Y:          return BTN_Y;
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return BTN_L1;
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R1;
      case SDL_CONTROLLER_BUTTON_START:      return BTN_START;
      case SDL_CONTROLLER_BUTTON_BACK:       return BTN_SELECT;
      case SDL_CONTROLLER_BUTTON_GUIDE:      return BTN_MENU;
      default: break;
      }
      break;
    default:
      break;
    }
  }
  return BTN_NONE;
}

void plat_present(void) {
  SDL_RenderPresent(g_ren);
}

void plat_suspend_hook(void (*on_resume)(void)) {
  g_on_resume = on_resume;
}

void plat_quit(void) {
  if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
  if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = NULL; }
  TTF_Quit();
  SDL_Quit();
  LOGI("platform shut down");
}
