/* platform/evdev_tg5040.c: raw evdev UI input for the Brick Pro (tg5040).
 *
 * Why: SDL 2.26 in the TrimUI SDK does not enumerate the built-in pad
 * ("TRIMUI Player1": js0 + event3); only a stale uinput "Xbox 360 Controller"
 * shows up and it emits nothing (tg-0 spike, ops/LOG.md 2026-10-18). Per the
 * recorded decision (ops/reviews/tg-0-input.md) the tg5040 UI reads the pad
 * via raw evdev; desktop/SP/MMP keep the SDL joystick/gamecontroller path.
 *
 * Measured device facts (tg-0, raw evdev on event3):
 *   EV_KEY  A=304 B=305 X=307 Y=308 L1=310 R1=311 Select=314 Start=315
 *           Menu=316 L3=317 R3=318. L3/R3 have no pp_btn; KEY 59/60 (the two
 *           assignable buttons) stay unbound until the owner picks actions.
 *   EV_ABS  L2=axis 2 R2=axis 5 (analog; midpoint crossing = press),
 *           D-pad X=axis 16 Y=axis 17 (hat; kernel sign convention -1/0/+1,
 *           to be confirmed on-device in the Phase 0 pass).
 *
 * The code->button mapping lives in input_map.c (pp_tg5040_*, unit-tested);
 * this file is the thin I/O + edge-detection layer. Reading is never grabbed
 * (no EVIOCGRAB): NextUI's trimui_inputd/keymon keep reading the same node,
 * and the player lane's evdev thread coexists the same way.
 */
#include "platform/evdev_tg5040.h"
#include "ui/input_map.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* linux/input.h #defines BTN_LEFT/BTN_RIGHT as the mouse-button codes
 * (0x110/0x111), which would collide with the pp_btn enumerators of the same
 * name and silently break hold-repeat for the D-pad. Restore the pp_btn
 * meanings (the kernel's BTN_SOUTH below is the one we DO want as a code). */
#ifdef BTN_LEFT
#undef BTN_LEFT
#endif
#ifdef BTN_RIGHT
#undef BTN_RIGHT
#endif

/* EV_KEY codes with held state the UI repeat logic cares about (BTN_TL/TR). */
#define TG_L1_KEY 310
#define TG_R1_KEY 311
#define TG_KEY_STATES 512   /* all mapped codes are < 512 */

static int g_fd = -1;
static char g_path[300];
static int g_thr_l = 128, g_thr_r = 128;   /* trigger midpoints (see open) */

/* Press queue: plat_poll_button consumes one per call, like the SDL path. */
#define PRESS_QUEUE 8
static pp_btn g_presses[PRESS_QUEUE];
static int g_q_head = 0, g_q_count = 0;

/* Edge/hold state. g_held_repeatable is the last pressed D-pad/L1/R1 input;
 * held() clears it when its source (hat axis or key) is no longer engaged. */
static pp_btn g_held_repeatable = BTN_NONE;
static int g_hat_x = 0, g_hat_y = 0;
static unsigned char g_key_state[TG_KEY_STATES];
static int g_trig_l = 0, g_trig_r = 0;

static void queue_press(pp_btn b) {
  if (b == BTN_NONE) return;
  if (g_q_count == PRESS_QUEUE) {
    memmove(g_presses, g_presses + 1, sizeof(pp_btn) * (PRESS_QUEUE - 1));
    g_q_count--;
    LOGW("input: evdev press queue full; dropped the oldest press");
  }
  g_presses[(g_q_head + g_q_count) % PRESS_QUEUE] = b;
  g_q_count++;
}

static void note_repeatable(pp_btn b) {
  if (b == BTN_UP || b == BTN_DOWN || b == BTN_LEFT || b == BTN_RIGHT ||
      b == BTN_L1 || b == BTN_R1)
    g_held_repeatable = b;
}

static void on_key(int code, int value) {
  if (code >= 0 && code < TG_KEY_STATES) g_key_state[code] = value > 0;
  pp_btn b = pp_tg5040_key_to_btn(code);
  if (b == BTN_NONE) {
    if (value == 1)
      LOGI("input: evdev key %d unbound (L3/R3/assignable)", code);
    return;
  }
  if (value == 1) {   /* press only: value 2 is device auto-repeat, we repeat */
    queue_press(b);
    note_repeatable(b);
    LOGI("input: evdev key %d -> %s", code, pp_btn_name(b));
  }
}

/* Direction of one hat axis at value v (BTN_NONE when centred/invalid). */
static pp_btn hat_axis_btn(int axis, int v) {
  return pp_tg5040_hat_to_btn(axis, v);
}

static void on_abs(int code, int value) {
  if (code == 16 || code == 17) {         /* D-pad hat: edge = new direction */
    pp_btn before = hat_axis_btn(code, code == 16 ? g_hat_x : g_hat_y);
    pp_btn now = hat_axis_btn(code, value);
    if (code == 16) g_hat_x = value; else g_hat_y = value;
    if (now != before && now != BTN_NONE) {
      queue_press(now);
      note_repeatable(now);
      LOGI("input: evdev hat %d -> %s", code, pp_btn_name(now));
    }
    return;
  }
  pp_btn b = pp_tg5040_trigger_to_btn(code);
  if (b == BTN_NONE) return;
  int thr = code == 2 ? g_thr_l : g_thr_r;
  int *state = code == 2 ? &g_trig_l : &g_trig_r;
  int on = value > thr;
  if (on && !*state) {
    *state = 1;
    queue_press(b);
    LOGI("input: evdev trigger %d -> %s", code, pp_btn_name(b));
  } else if (!on) {
    *state = 0;
  }
}

pp_btn pp_evdev_tg5040_poll(void) {
  if (g_fd < 0) return BTN_NONE;
  struct input_event ev[16];
  for (;;) {
    ssize_t r = read(g_fd, ev, sizeof ev);
    if (r == 0) break;
    if (r < 0) {
      if (errno == EINTR) continue;
      break;   /* EAGAIN: drained */
    }
    size_t n = (size_t)r / sizeof ev[0];
    for (size_t k = 0; k < n; k++) {
      if (ev[k].type == EV_KEY) on_key((int)ev[k].code, (int)ev[k].value);
      else if (ev[k].type == EV_ABS) on_abs((int)ev[k].code, (int)ev[k].value);
      /* EV_SYN and anything else: ignored */
    }
  }
  if (g_q_count == 0) return BTN_NONE;
  pp_btn b = g_presses[g_q_head];
  g_q_head = (g_q_head + 1) % PRESS_QUEUE;
  g_q_count--;
  return b;
}

pp_btn pp_evdev_tg5040_held(void) {
  switch (g_held_repeatable) {
  case BTN_UP:    if (g_hat_y >= 0) g_held_repeatable = BTN_NONE; break;
  case BTN_DOWN:  if (g_hat_y <= 0) g_held_repeatable = BTN_NONE; break;
  case BTN_LEFT:  if (g_hat_x >= 0) g_held_repeatable = BTN_NONE; break;
  case BTN_RIGHT: if (g_hat_x <= 0) g_held_repeatable = BTN_NONE; break;
  case BTN_L1:    if (!g_key_state[TG_L1_KEY]) g_held_repeatable = BTN_NONE; break;
  case BTN_R1:    if (!g_key_state[TG_R1_KEY]) g_held_repeatable = BTN_NONE; break;
  default:        g_held_repeatable = BTN_NONE; break;
  }
  return g_held_repeatable;
}

/* Press threshold at the axis midpoint, from the device's own reported range
 * (EVIOCGABS); 128 (half of 0..255) when the query fails. */
static int trigger_threshold(int axis) {
  struct input_absinfo ai;
  if (ioctl(g_fd, EVIOCGABS(axis), &ai) == 0 && ai.maximum > ai.minimum)
    return ai.minimum + (ai.maximum - ai.minimum) / 2;
  return 128;
}

int pp_evdev_tg5040_open(void) {
  pp_evdev_tg5040_close();
  DIR *d = opendir("/dev/input");
  if (!d) {
    LOGE("input: cannot read /dev/input (%s)", strerror(errno));
    return -1;
  }
  struct dirent *e;
  char path[300];
  int fb_fd = -1;                 /* fallback: first BTN_SOUTH node */
  char fb_name[128] = "", fb_path[300] = "";
  while ((e = readdir(d)) != NULL) {
    if (strncmp(e->d_name, "event", 5) != 0) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1];
    memset(bits, 0, sizeof bits);
    int south =
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) >= 0 &&
        ((bits[BTN_SOUTH / (8 * sizeof(unsigned long))] >>
          (BTN_SOUTH % (8 * sizeof(unsigned long))) & 1));
    if (!south) {
      close(fd);
      continue;
    }
    char name[128] = "";
    if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) < 0) name[0] = '\0';
    name[sizeof name - 1] = '\0';
    if (strcmp(name, "TRIMUI Player1") == 0) {
      /* The real built-in pad: wins over any BTN_SOUTH node seen earlier
       * (readdir order is unstable; the stale uinput "Xbox 360 Controller"
       * also advertises BTN_SOUTH and emits nothing - tg-0). */
      if (fb_fd >= 0) { close(fb_fd); fb_fd = -1; }
      g_fd = fd;
      snprintf(g_path, sizeof g_path, "%s", path);
      LOGI("input: evdev pad '%s' on %s (name match)", name, g_path);
      break;
    }
    if (fb_fd < 0) {
      fb_fd = fd;
      snprintf(fb_name, sizeof fb_name, "%s", name[0] ? name : "?");
      snprintf(fb_path, sizeof fb_path, "%s", path);
      LOGI("input: evdev candidate '%s' on %s (BTN_SOUTH, not the named pad)",
           fb_name, fb_path);
    } else {
      close(fd);
    }
  }
  closedir(d);
  if (g_fd < 0 && fb_fd >= 0) {
    g_fd = fb_fd;
    snprintf(g_path, sizeof g_path, "%s", fb_path);
    LOGW("input: no 'TRIMUI Player1' under /dev/input/event*; using '%s' on %s "
         "(if the pad is dead this node is stale)", fb_name, g_path);
  }
  if (g_fd < 0) {
    LOGE("input: no evdev gamepad (BTN_SOUTH) under /dev/input/event*; "
         "pad buttons are dead this run (SDL cannot enumerate the pad on "
         "tg5040 either - check /dev/input/event* read permission)");
    return -1;
  }
  g_thr_l = trigger_threshold(2);
  g_thr_r = trigger_threshold(5);
  g_q_head = g_q_count = 0;
  g_hat_x = g_hat_y = 0;
  memset(g_key_state, 0, sizeof g_key_state);
  g_trig_l = g_trig_r = 0;
  g_held_repeatable = BTN_NONE;
  return 0;
}

void pp_evdev_tg5040_close(void) {
  if (g_fd >= 0) {
    close(g_fd);
    g_fd = -1;
    LOGI("input: evdev pad closed (%s)", g_path);
  }
}
