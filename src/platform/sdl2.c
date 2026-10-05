/* platform/sdl2.c: SDL2 backend for desktop and RG35XX SP.
 * Implements the frozen platform.h contract. Also exposes plat_renderer()
 * so the UI layer can draw with SDL_Renderer directly.
 */
#include "platform/platform.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include "ui/input_map.h"
#include "log.h"
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
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER |
               SDL_INIT_GAMECONTROLLER) != 0) {
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

/* SDL never repeats controller events, so repeat held D-pad/L1/R1 here
 * (same feel as keyboard repeat). */
#define PAD_REPEAT_DELAY_MS 300
#define PAD_REPEAT_RATE_MS   80
static pp_btn g_pad_held = BTN_NONE;
static Uint32 g_pad_next = 0;

/* input_map.c maps PP_PAD_* (tested without SDL); they must equal SDL's values. */
#define PP_PAD_CHECK(pp, sdl) typedef char pp_pad_check_##pp[((pp) == (sdl)) ? 1 : -1]
PP_PAD_CHECK(PP_PAD_A, SDL_CONTROLLER_BUTTON_A);
PP_PAD_CHECK(PP_PAD_B, SDL_CONTROLLER_BUTTON_B);
PP_PAD_CHECK(PP_PAD_X, SDL_CONTROLLER_BUTTON_X);
PP_PAD_CHECK(PP_PAD_Y, SDL_CONTROLLER_BUTTON_Y);
PP_PAD_CHECK(PP_PAD_BACK, SDL_CONTROLLER_BUTTON_BACK);
PP_PAD_CHECK(PP_PAD_GUIDE, SDL_CONTROLLER_BUTTON_GUIDE);
PP_PAD_CHECK(PP_PAD_START, SDL_CONTROLLER_BUTTON_START);
PP_PAD_CHECK(PP_PAD_LEFTSTICK, SDL_CONTROLLER_BUTTON_LEFTSTICK);
PP_PAD_CHECK(PP_PAD_RIGHTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
PP_PAD_CHECK(PP_PAD_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
PP_PAD_CHECK(PP_PAD_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
PP_PAD_CHECK(PP_PAD_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
PP_PAD_CHECK(PP_PAD_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
PP_PAD_CHECK(PP_PAD_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
PP_PAD_CHECK(PP_PAD_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

#define PAD_TRIGGER_ON 16000  /* trigger axis threshold for L2/R2 */
static int g_trig_l = 0, g_trig_r = 0;

/* Log a press with what it mapped to, so device logs show the real layout. */
static pp_btn log_press(const char *src, int code, pp_btn b) {
  LOGI("input: %s %d -> %s", src, code, pp_btn_name(b));
  return b;
}

static void on_joystick_added(int index) {
  const char *name = SDL_JoystickNameForIndex(index);
  if (SDL_IsGameController(index)) {
    SDL_GameController *gc = SDL_GameControllerOpen(index);
    LOGI("input: controller %d '%s' (game controller%s)", index, name ? name : "?",
         gc ? "" : ", open failed");
  } else {
    /* No SDL mapping: open it anyway so raw codes reach the log. */
    SDL_Joystick *js = SDL_JoystickOpen(index);
    LOGW("input: joystick %d '%s' has no game-controller mapping; buttons unmapped "
         "(raw codes logged)%s", index, name ? name : "?", js ? "" : ", open failed");
  }
}

static int pad_repeats(pp_btn b) {
  return b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT ||
         b == BTN_L1 || b == BTN_R1;
}

pp_btn plat_poll_button(void) {
  SDL_Event ev;
  pp_btn b;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_QUIT:
      g_quit_requested = 1;
      LOGI("input: SDL_QUIT (window close or signal)");
      return BTN_MENU;
    case SDL_KEYDOWN:
      return log_press("key", (int)ev.key.keysym.sym, pp_keycode_to_btn(ev.key.keysym.sym));
    case SDL_JOYDEVICEADDED:
      on_joystick_added(ev.jdevice.which);
      break;
    case SDL_CONTROLLERBUTTONDOWN:
      b = log_press("pad button", ev.cbutton.button, pp_pad_button_to_btn(ev.cbutton.button));
      if (b == BTN_NONE) break;
      if (pad_repeats(b)) {
        g_pad_held = b;
        g_pad_next = SDL_GetTicks() + PAD_REPEAT_DELAY_MS;
      }
      return b;
    case SDL_CONTROLLERBUTTONUP:
      if (pp_pad_button_to_btn(ev.cbutton.button) == g_pad_held) g_pad_held = BTN_NONE;
      break;
    case SDL_CONTROLLERAXISMOTION:
      /* Triggers as L2/R2 (edge-triggered). Never MENU. */
      if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
          ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
        int left = ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        int *state = left ? &g_trig_l : &g_trig_r;
        int on = ev.caxis.value > PAD_TRIGGER_ON;
        if (on && !*state) { *state = 1; return log_press("pad trigger", ev.caxis.axis, left ? BTN_L2 : BTN_R2); }
        if (!on) *state = 0;
      }
      break;
    case SDL_JOYBUTTONDOWN:
      /* Game controllers also emit joystick events; only log unmapped devices. */
      if (!SDL_GameControllerFromInstanceID(ev.jbutton.which))
        LOGI("input: raw joystick %d button %d (unmapped)", (int)ev.jbutton.which, ev.jbutton.button);
      break;
    case SDL_JOYHATMOTION:
      if (!SDL_GameControllerFromInstanceID(ev.jhat.which) && ev.jhat.value)
        LOGI("input: raw joystick %d hat %d value %d (unmapped)", (int)ev.jhat.which,
             ev.jhat.hat, ev.jhat.value);
      break;
    default:
      break;
    }
  }
  if (g_pad_held != BTN_NONE && SDL_TICKS_PASSED(SDL_GetTicks(), g_pad_next)) {
    g_pad_next = SDL_GetTicks() + PAD_REPEAT_RATE_MS;
    return g_pad_held;
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
  if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = NULL; }
  if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
  TTF_Quit();
  SDL_Quit();
  LOGI("platform shut down");
}
