/* platform/sdl2.c: SDL2 backend for desktop and RG35XX SP.
 * Implements the frozen platform.h contract. Also exposes plat_renderer()
 * so the UI layer can draw with SDL_Renderer directly.
 */
#include "platform/platform.h"
#include "platform/suspend.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include "ui/input_map.h"
#include "log.h"

#if defined(PP_PLATFORM_TG5040)
/* SDL 2.26 (TrimUI SDK) cannot enumerate the Brick Pro pad, so input comes
 * from raw evdev instead (decision: ops/reviews/tg-0-input.md). */
#include "platform/evdev_tg5040.h"
#endif

#if defined(PP_PLATFORM_SP) || defined(PP_PLATFORM_MMP) || defined(PP_PLATFORM_TG5040)
#define PP_HANDHELD_BUILD 1   /* unknown controller = built-in Nintendo-layout pad */
#else
#define PP_HANDHELD_BUILD 0
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static int g_quit_requested = 0;
static void (*g_on_resume)(void) = NULL;
static int g_win_w = 640, g_win_h = 480;  /* real display size, kept for video resume */

/* Return the renderer for the UI drawing layer. NULL until plat_init. */
SDL_Renderer *plat_renderer(void) { return g_ren; }
SDL_Window *plat_window(void) { return g_win; }

/* The window size plat_init chose (real display size on tg5040). */
void plat_display_size(int *w, int *h) {
  if (w) *w = g_win_w;
  if (h) *h = g_win_h;
}

/* Wall clock in ms. Monotonic clocks (SDL_GetTicks) freeze across suspend, so
 * sleep/wake detection has to use the wall clock. */
static long wall_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* on_resume fires once per wake: SDL_APP_WILLENTERFOREGROUND and
 * SDL_APP_DIDENTERFOREGROUND both arrive for the same resume, and the
 * wall-clock gap check can coincide with them. */
static long g_resume_fired_at = 0;
static long g_last_tick_wall_ms = 0;

static void fire_resume(const char *why) {
  if (!g_on_resume) return;
  long now = wall_now_ms();
  if (g_resume_fired_at && now - g_resume_fired_at < 2000) return;
  g_resume_fired_at = now;
  LOGI("resume: %s (Wi-Fi reconnect pending)", why);
  g_on_resume();
}

/* Window + renderer (also used to rebuild them after the external player). */
static int create_video(int ww, int hh) {
#if defined(PP_PLATFORM_TG5040)
  /* Give the window a GLES3 context so libmpv's render API can resolve GL
   * symbols via SDL_GL_GetProcAddress and present with SDL_GL_SwapWindow. */
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#endif
  g_win = SDL_CreateWindow("PocketPlex",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           ww, hh,
#if defined(PP_PLATFORM_TG5040)
                           SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL
#else
                           SDL_WINDOW_SHOWN
#endif
  );
  if (!g_win) {
    LOGE("SDL_CreateWindow: %s", SDL_GetError());
    return -1;
  }
  g_ren = SDL_CreateRenderer(g_win, -1,
                             SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
  if (!g_ren) {
    LOGE("SDL_CreateRenderer: %s", SDL_GetError());
    SDL_DestroyWindow(g_win);
    g_win = NULL;
    return -1;
  }
#if defined(PP_PLATFORM_TG5040)
  /* libmpv's render API drives OpenGL directly, so TG5040 needs a renderer that
     exposes a GL context. SDL_CreateRenderer(-1) may create one for us; if it
     picked a software driver there is no GL context and the render API can't
     work — log what we got and refuse to start so the failure is obvious on the
     device pass. (We do NOT call SDL_GL_CreateContext here yet; first confirm a
     hardware renderer provides a context.) */
  {
    SDL_RendererInfo info;
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    const char *rname = "(unknown)";
    int is_software = 1;
    if (SDL_GetRendererInfo(g_ren, &info) == 0) {
      rname = info.name;
      is_software = !(info.flags & SDL_RENDERER_ACCELERATED);
    }
    LOGI("tg5040: renderer=%s software=%d gl_current_ctx=%s (after SDL_CreateRenderer)",
         rname, is_software, ctx ? "yes" : "no");
    if (is_software) {
      LOGE("tg5040: renderer '%s' is software (no GL context); mpv render API requires a "
           "GL context — refusing to start", rname);
      SDL_DestroyRenderer(g_ren); g_ren = NULL;
      SDL_DestroyWindow(g_win); g_win = NULL;
      return -1;
    }
  }
#endif
  return 0;
}

static void destroy_video(void) {
  if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = NULL; }
  if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
}

void plat_video_suspend(void) {
#if defined(PP_PLATFORM_TG5040)
  /* libmpv render-API embed: keep the window + GL context so mpv can draw into
   * the app's surface. The menu is held off by no_present (ui.c); nothing else
   * to tear down here. */
  LOGI("video: kept for libmpv render API (tg5040)");
#else
  destroy_video();
  SDL_QuitSubSystem(SDL_INIT_VIDEO);  /* events/controllers stay up (own refcounts) */
  LOGI("video: suspended for the external player");
#endif
}

int plat_video_resume(void) {
  if (g_ren) return 0;
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    LOGE("video: SDL_InitSubSystem: %s", SDL_GetError());
    return -1;
  }
  if (create_video(g_win_w, g_win_h) != 0) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return -1;
  }
  LOGI("video: window and renderer recreated");
  return 0;
}

int plat_init(int *w, int *h) {
  /* SDL maps known Nintendo (Switch) pads by label with this on; other pads
   * are positional and input_map applies the handheld layout itself. */
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "1");
  int sdl_flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER;
#if !defined(PP_PLATFORM_TG5040)
  sdl_flags |= SDL_INIT_GAMECONTROLLER;  /* tg5040: raw evdev input instead */
#endif
  if (SDL_Init(sdl_flags) != 0) {
    LOGE("SDL_Init: %s", SDL_GetError());
    return -1;
  }
  if (TTF_Init() != 0) {
    LOGE("TTF_Init: %s", TTF_GetError());
    SDL_Quit();
    return -1;
  }

  int ww = 640, hh = 480;
#if defined(PP_PLATFORM_TG5040)
  /* The panel is 1024x768; ask SDL for the real mode instead of assuming the
   * 640x480 design size. */
  SDL_DisplayMode dm;
  if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
    ww = dm.w;
    hh = dm.h;
    LOGI("display: SDL reports %dx%d", ww, hh);
  } else {
    LOGW("display: SDL_GetCurrentDisplayMode failed (%s); assuming 640x480",
         SDL_GetError());
  }
#endif
  {  /* PP_FAKE_DISPLAY=WxH: fake the display for desktop layout checks. */
    int fw = 0, fh = 0;
    if (pp_parse_display_size(getenv("PP_FAKE_DISPLAY"), &fw, &fh) == 0) {
      LOGI("display: PP_FAKE_DISPLAY overrides to %dx%d", fw, fh);
      ww = fw;
      hh = fh;
    }
  }
  if (create_video(ww, hh) != 0) {
    TTF_Quit();
    SDL_Quit();
    return -1;
  }
  g_win_w = ww;
  g_win_h = hh;

  if (w) *w = ww;
  if (h) *h = hh;
  g_quit_requested = 0;
  LOGI("platform ready: window %dx%d", ww, hh);
#if defined(PP_PLATFORM_TG5040)
  if (pp_evdev_tg5040_open() != 0)
    LOGW("input: continuing without the pad (SDL keyboard only)");
#else
  LOGI("input: default layout %s until a controller is found",
       pp_pad_layout_name(pp_pad_layout_for(NULL, 0, PP_HANDHELD_BUILD)));
  pp_pad_set_layout(pp_pad_layout_for(NULL, 0, PP_HANDHELD_BUILD));
#endif
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

#if !defined(PP_PLATFORM_TG5040)   /* trigger axes come from evdev on tg5040 */
#define PAD_TRIGGER_ON 16000  /* trigger axis threshold for L2/R2 */
static int g_trig_l = 0, g_trig_r = 0;
#endif /* !PP_PLATFORM_TG5040 */

/* Log a press with what it mapped to, so device logs show the real layout. */
static pp_btn log_press(const char *src, int code, pp_btn b) {
  LOGI("input: %s %d -> %s", src, code, pp_btn_name(b));
  return b;
}

#if !defined(PP_PLATFORM_TG5040)   /* SDL joystick/gamecontroller path */
static void on_joystick_added(int index) {
  const char *name = SDL_JoystickNameForIndex(index);
  if (SDL_IsGameController(index)) {
    SDL_GameController *gc = SDL_GameControllerOpen(index);
    LOGI("input: controller %d '%s' (game controller%s)", index, name ? name : "?",
         gc ? "" : ", open failed");
    int label_mapped = 0;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_GameControllerType type = SDL_GameControllerTypeForIndex(index);
    label_mapped = type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
#if SDL_VERSION_ATLEAST(2, 24, 0)
    label_mapped |= type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
                    type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
                    type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
#endif
#endif
    pp_pad_layout layout = pp_pad_layout_for(name, label_mapped, PP_HANDHELD_BUILD);
#if defined(PP_PLATFORM_TG5040)
    /* The built-in TrimUI pad prints A on the east button, but SDL reports it as "Xbox 360
     * Controller", which the name rules read as positional. It is the only pad in scope here. */
    layout = PP_LAYOUT_NINTENDO;
#endif
    pp_pad_set_layout(layout);
    LOGI("input: layout %s for '%s' (%s; swap_ab=%d)", pp_pad_layout_name(layout),
         name ? name : "?",
         layout == PP_LAYOUT_NINTENDO ? "A = east, X = north" : "A = south, X = west",
         ui_is_swap_ab());
  } else {
    /* No SDL mapping: open it anyway so raw codes reach the log. */
    SDL_Joystick *js = SDL_JoystickOpen(index);
    LOGW("input: joystick %d '%s' has no game-controller mapping; buttons unmapped "
         "(raw codes logged)%s", index, name ? name : "?", js ? "" : ", open failed");
  }
}
#endif /* !PP_PLATFORM_TG5040 */

static int pad_repeats(pp_btn b) {
  return b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT ||
         b == BTN_L1 || b == BTN_R1;
}

pp_btn plat_poll_button(void) {
  /* Sleep/wake: this runs once per main-loop tick. On the SP, suspend freezes
   * the process; the first tick after wake sees a wall-clock jump. (SDL's
   * SDL_APP_* foreground events below cover platforms that send them.) */
  long wall = wall_now_ms();
  if (pp_wall_gap_is_resume(g_last_tick_wall_ms, wall, PP_RESUME_GAP_MS))
    fire_resume("wall-clock gap");
  g_last_tick_wall_ms = wall;

#if defined(PP_PLATFORM_TG5040)
  /* Raw evdev pad (SDL cannot enumerate it on this device). One queued press
   * per call, like the SDL path; held state feeds the shared repeat block
   * at the bottom so D-pad/L1/R1 repeats feel identical on both backends. */
  pp_btn eb = pp_evdev_tg5040_poll();
  if (eb != BTN_NONE) {
    if (pad_repeats(eb)) {
      g_pad_held = eb;
      g_pad_next = SDL_GetTicks() + PAD_REPEAT_DELAY_MS;
    }
    return eb;
  }
  g_pad_held = pp_evdev_tg5040_held();
#endif

  SDL_Event ev;
#if !defined(PP_PLATFORM_TG5040)
  pp_btn b;
#endif
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_QUIT:
      g_quit_requested = 1;
      LOGI("input: SDL_QUIT (window close or signal)");
      return BTN_MENU;
    case SDL_APP_WILLENTERFOREGROUND:
      fire_resume("SDL_APP_WILLENTERFOREGROUND");
      break;
    case SDL_APP_DIDENTERFOREGROUND:
      fire_resume("SDL_APP_DIDENTERFOREGROUND");
      break;
    case SDL_KEYDOWN:
      return log_press("key", (int)ev.key.keysym.sym, pp_keycode_to_btn(ev.key.keysym.sym));
#if !defined(PP_PLATFORM_TG5040)
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
#endif /* !PP_PLATFORM_TG5040 */
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
  if (g_ren) SDL_RenderPresent(g_ren);
}

void plat_suspend_hook(void (*on_resume)(void)) {
  g_on_resume = on_resume;
}

void plat_quit(void) {
#if defined(PP_PLATFORM_TG5040)
  pp_evdev_tg5040_close();
#endif
  destroy_video();
  TTF_Quit();
  SDL_Quit();
  LOGI("platform shut down");
}
