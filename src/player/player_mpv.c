/* player/player_mpv.c: player.h on top of an external mpv process.
 *
 * mpv runs as a child (posix_spawn) with --input-ipc-server=<unix socket>. player_poll asks it for
 * time-pos over that socket. Positions are absolute because the caller builds the transcode URL with
 * offset=0 and we pass the resume point as --start (see docs/devices.md, "Stream position").
 *
 * Buttons: on the SP, mpv can't read the gamepad while drawing with --vo=sdl, so a thread reads
 * evdev and sends IPC commands (mpv_map_event). On desktop, mpv's own window takes the keyboard
 * through a generated input.conf.
 *
 * Environment overrides: PP_MPV_BIN (default "mpv"), PP_MPV_VO (default "sdl" on devices, mpv's
 * own choice on desktop), PP_MPV_AO.
 * mpv's output goes to /dev/null: it prints the URL, and the URL carries the token.
 */
#include "player/player.h"
#include "log.h"

#if defined(PP_PLATFORM_TG5040)
#include <SDL.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>     /* pulls in mpv/render.h (mpv_render_context, params) */
#include "ui/ui_platform.h"    /* plat_window() */
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#if defined(__linux__) && !defined(PP_PLATFORM_DESKTOP)
#define PP_MPV_EVDEV 1
#include <dirent.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <pthread.h>
#endif

extern char **environ;

#define PP_MPV_MAX_ARGS 32
#define PP_MPV_ARG_LEN  160
#define PP_EV_KEY 1
#define PP_EV_ABS 3
#define PP_MPV_REPEAT_MS 500

/* The parts of fb_var_screeninfo that mpv can leave changed (portable mirror, so it's testable). */
typedef struct {
  unsigned xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel;
} pp_fbinfo;
typedef enum { PP_FB_NONE, PP_FB_PAN, PP_FB_PUT } pp_fb_action;

struct pp_player {
  pp_fbinfo fb_saved;     /* fb0 as the app's renderer left it at player_start */
  int fb_saved_ok;
  pid_t pid;
  int exited, exit_code;
  int fd;                 /* IPC connection used by player_poll; -1 until connected */
  int req;
  long pos_ms;
  char sock[104], conf[104];
  char rbuf[4096];
  size_t rlen;
#ifdef PP_MPV_EVDEV
  pthread_t input_thread;
  int input_running;
  volatile int input_stop;
#endif
#if defined(PP_PLATFORM_TG5040)
  mpv_handle *mpv;        /* in-process libmpv handle (tg5040 only) */
  mpv_render_context *rc; /* libmpv render context bound to our GL ctx */
  int mpv_update;         /* set by the render-context update callback */
  int mpv_finished;       /* MPV_EVENT_SHUTDOWN / END_FILE seen */
  int mpv_error;          /* playback error flag */
#endif
};

#if defined(PP_PLATFORM_TG5040)
/* mpv's render API takes a 2-arg (ctx, name) get_proc_address; wrap SDL's. */
static void *tg_gl_get_proc(void *ctx, const char *name) {
  (void)ctx;
  return SDL_GL_GetProcAddress(name);
}
/* Called by mpv from whatever thread decides a new frame is ready. */
static void tg_mpv_update(void *ctx) {
  ((pp_player *)ctx)->mpv_update = 1;
}
#endif

/* Keyboard bindings for mpv's own window (desktop). Devices use the evdev thread instead. */
static const char mpv_input_conf[] =
  "SPACE cycle pause\nENTER cycle pause\nKP_ENTER cycle pause\n"
  "LEFT seek -10\nRIGHT seek 10\nPGUP seek 60\nPGDWN seek -60\nUP seek 300\nDOWN seek -300\n"
  "TAB show-progress\nq quit\nESC quit\nBS quit\nCLOSE_WIN quit\n";

/* Fills argv (NULL-terminated) for mpv. Formatted args live in bufs; url is referenced, not copied.
 * Returns the argument count. conf and vo may be NULL. */
static int mpv_build_args(char **argv, char bufs[][PP_MPV_ARG_LEN], const char *bin, const char *url,
                          long start_ms, const char *sock, const char *conf, const char *vo) {
  static const char *fixed[] = {
    "--no-config", "--terminal=no", "--idle=no", "--keep-open=no", "--fullscreen",
    "--hwdec=no", "--osd-level=1", "--no-input-default-bindings", "--title=PocketPlex",
    "--cache=yes", "--demuxer-max-bytes=32MiB", "--demuxer-max-back-bytes=16MiB",
    "--network-timeout=20",
  };
  const char *ao = getenv("PP_MPV_AO");
  int n = 0, b = 0;
  size_t i;

  if (start_ms < 0) start_ms = 0;
  argv[n++] = (char *)bin;
  for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++) argv[n++] = (char *)fixed[i];
#define PP_ARGF(...) (snprintf(bufs[b], PP_MPV_ARG_LEN, __VA_ARGS__), argv[n++] = bufs[b++])
  PP_ARGF("--input-ipc-server=%s", sock);
  PP_ARGF("--start=%ld.%03ld", start_ms / 1000, start_ms % 1000);
  if (conf) PP_ARGF("--input-conf=%s", conf);
  if (vo && *vo) PP_ARGF("--vo=%s", vo);
  if (ao && *ao) PP_ARGF("--ao=%s", ao);
#undef PP_ARGF
  argv[n++] = (char *)url;
  argv[n] = NULL;
  return n;
}

/* Parses one line of mpv IPC output. 1 = the reply to request_id with a numeric data field (in *out),
 * -1 = the reply to request_id but an error (e.g. "property unavailable" while loading),
 * 0 = some other line (events, other requests). */
static int mpv_parse_reply(const char *line, int request_id, double *out) {
  const char *p = strstr(line, "\"request_id\":"), *d;
  char *end;
  double v;
  if (!p || atoi(p + 13) != request_id) return 0;
  if (!strstr(line, "\"error\":\"success\"")) return -1;
  if (!(d = strstr(line, "\"data\":"))) return -1;
  v = strtod(d + 7, &end);
  if (end == d + 7) return -1;
  *out = v;
  return 1;
}

/* Maps an SP controller evdev event to an mpv JSON command array, or NULL. Codes are from
 * docs/devices.md "Buttons". Seeks on shoulder buttons auto-repeat (value 2); the rest don't.
 * Unused on desktop builds (no evdev thread there), but always compiled so it stays tested. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static const char *mpv_map_event(int type, int code, int value) {
  if (type == PP_EV_ABS) {
    if (code == 16) return value < 0 ? "[\"seek\",-10,\"relative\"]" : value > 0 ? "[\"seek\",10,\"relative\"]" : NULL;
    if (code == 17) return value < 0 ? "[\"seek\",300,\"relative\"]" : value > 0 ? "[\"seek\",-300,\"relative\"]" : NULL;
    return NULL;
  }
  if (type != PP_EV_KEY || value == 0) return NULL;
  switch (code) {
    case 308: return "[\"seek\",-60,\"relative\"]";  /* L1 */
    case 309: return "[\"seek\",60,\"relative\"]";   /* R1 */
  }
  if (value != 1) return NULL;
  switch (code) {
    case 304: case 311: return "[\"cycle\",\"pause\"]";  /* A, Start */
    case 305: case 312: return "[\"quit\"]";             /* B, Menu */
    case 307: case 310: return "[\"show-progress\"]";    /* X, Select */
  }
  return NULL;
}

/* Monotonic milliseconds. Not wall time: the SP has no RTC, and NTP steps the clock after Wi-Fi rejoins. */
static long mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* poll() timeout for a deadline, clamped to [0, timeout_ms]. */
static int mpv_wait_left(long deadline, long now, int timeout_ms) {
  long left = deadline - now;
  return left <= 0 ? 0 : left > timeout_ms ? timeout_ms : (int)left;
}

/* Held buttons auto-repeat at ~30 Hz, and every seek restarts the PMS transcoder. A press (value 1)
 * always passes; repeats (value 2) pass at most once per PP_MPV_REPEAT_MS. Device-only, like mpv_map_event. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static int mpv_repeat_ok(long *last_ms, int value, long now) {
  if (value == 2 && now - *last_ms < PP_MPV_REPEAT_MS) return 0;
  *last_ms = now;
  return 1;
}

static int ipc_connect(const char *path) {
  struct sockaddr_un sa;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  { int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
  memset(&sa, 0, sizeof sa);
  sa.sun_family = AF_UNIX;
  snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
  if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); return -1; }
  return fd;
}

static int ipc_send(int fd, const char *cmd_array, int request_id) {
  char msg[256];
  int n = snprintf(msg, sizeof msg, "{\"command\":%s,\"request_id\":%d}\n", cmd_array, request_id);
#ifdef MSG_NOSIGNAL
  return send(fd, msg, (size_t)n, MSG_NOSIGNAL) == n ? 0 : -1;
#else
  return send(fd, msg, (size_t)n, 0) == n ? 0 : -1;
#endif
}

/* Asks for time-pos and waits up to timeout_ms for the reply. 1 = *sec set, 0 = unavailable or
 * timed out, -1 = the connection is gone. */
static int ipc_time_pos(pp_player *p, double *sec, int timeout_ms) {
  long deadline = mono_ms() + timeout_ms;
  int id = ++p->req;
  if (ipc_send(p->fd, "[\"get_property\",\"time-pos\"]", id) < 0) return -1;
  for (;;) {
    char *nl;
    struct pollfd pf;
    int left;
    ssize_t r;
    while ((nl = memchr(p->rbuf, '\n', p->rlen)) != NULL) {
      int rc;
      size_t used = (size_t)(nl - p->rbuf) + 1;
      *nl = 0;
      rc = mpv_parse_reply(p->rbuf, id, sec);
      memmove(p->rbuf, p->rbuf + used, p->rlen - used);
      p->rlen -= used;
      if (rc != 0) return rc > 0 ? 1 : 0;
    }
    if (p->rlen == sizeof p->rbuf) p->rlen = 0;   /* absurdly long line: drop it */
    if ((left = mpv_wait_left(deadline, mono_ms(), timeout_ms)) == 0) return 0;
    pf.fd = p->fd; pf.events = POLLIN; pf.revents = 0;
    if (poll(&pf, 1, left) <= 0) continue;
    r = read(p->fd, p->rbuf + p->rlen, sizeof p->rbuf - p->rlen);
    if (r <= 0) return -1;
    p->rlen += (size_t)r;
  }
}

/* What it takes to get from cur back to saved: nothing, a pan (Mali double-buffers by panning between
 * y=0 and y=yres, so mpv can leave the "wrong" buffer on screen), or a full mode put. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static pp_fb_action fb_restore_action(const pp_fbinfo *saved, const pp_fbinfo *cur) {
  if (saved->xres != cur->xres || saved->yres != cur->yres || saved->xres_virtual != cur->xres_virtual ||
      saved->yres_virtual != cur->yres_virtual || saved->bits_per_pixel != cur->bits_per_pixel)
    return PP_FB_PUT;
  if (saved->xoffset != cur->xoffset || saved->yoffset != cur->yoffset) return PP_FB_PAN;
  return PP_FB_NONE;
}

#ifdef PP_MPV_EVDEV
static void fb_copy(pp_fbinfo *o, const struct fb_var_screeninfo *v) {
  o->xres = v->xres; o->yres = v->yres; o->xres_virtual = v->xres_virtual; o->yres_virtual = v->yres_virtual;
  o->xoffset = v->xoffset; o->yoffset = v->yoffset; o->bits_per_pixel = v->bits_per_pixel;
}

static void fb_save(pp_player *p) {
  struct fb_var_screeninfo v;
  int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
  if (fd < 0) return;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0) {
    fb_copy(&p->fb_saved, &v);
    p->fb_saved_ok = 1;
    LOGI("player: fb0 before: %ux%u virt %ux%u offset %u,%u bpp %u", v.xres, v.yres, v.xres_virtual,
         v.yres_virtual, v.xoffset, v.yoffset, v.bits_per_pixel);
  }
  close(fd);
}

/* Called once, as soon as mpv's exit is seen and before the app presents again. On the SP, mpv left fb0
 * unchanged in every logged run (docs/devices.md), so this is a cheap guard plus a log line, not the
 * fix for the post-playback glitch. */
static void fb_restore(pp_player *p) {
  struct fb_var_screeninfo v;
  pp_fbinfo cur;
  pp_fb_action act;
  int fd, rc = 0;
  if (!p->fb_saved_ok || (fd = open("/dev/fb0", O_RDWR | O_CLOEXEC)) < 0) return;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0) {
    fb_copy(&cur, &v);
    act = fb_restore_action(&p->fb_saved, &cur);
    LOGI("player: fb0 after mpv: %ux%u virt %ux%u offset %u,%u bpp %u -> %s", v.xres, v.yres,
         v.xres_virtual, v.yres_virtual, v.xoffset, v.yoffset, v.bits_per_pixel,
         act == PP_FB_NONE ? "unchanged" : act == PP_FB_PAN ? "pan back" : "mode put back");
    if (act != PP_FB_NONE) {
      v.xoffset = p->fb_saved.xoffset; v.yoffset = p->fb_saved.yoffset;
      if (act == PP_FB_PUT) {
        v.xres = p->fb_saved.xres; v.yres = p->fb_saved.yres;
        v.xres_virtual = p->fb_saved.xres_virtual; v.yres_virtual = p->fb_saved.yres_virtual;
        v.bits_per_pixel = p->fb_saved.bits_per_pixel;
        v.activate = FB_ACTIVATE_NOW;
        rc = ioctl(fd, FBIOPUT_VSCREENINFO, &v);
      } else {
        rc = ioctl(fd, FBIOPAN_DISPLAY, &v);
      }
      if (rc < 0) LOGW("player: fb0 restore failed: %s", strerror(errno));
    }
  }
  ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
  close(fd);
}
#endif

static void reap(pp_player *p, int block) {
  int st;
  pid_t r;
  if (p->exited) return;
  do r = waitpid(p->pid, &st, block ? 0 : WNOHANG); while (r < 0 && errno == EINTR);
  if (r == p->pid || (r < 0 && errno == ECHILD)) {
    p->exited = 1;
    p->exit_code = r == p->pid ? (WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st)) : 0;
    LOGI("player: mpv exited (code %d) at %ld ms", p->exit_code, p->pos_ms);
#ifdef PP_MPV_EVDEV
    fb_restore(p);
#endif
  }
}

#ifdef PP_MPV_EVDEV
static int has_gamepad_keys(int fd) {
  unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1];
  memset(bits, 0, sizeof bits);
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0) return 0;
  return (bits[BTN_SOUTH / (8 * sizeof(unsigned long))] >> (BTN_SOUTH % (8 * sizeof(unsigned long)))) & 1;
}

static void *input_main(void *arg) {
  pp_player *p = arg;
  struct pollfd pf[8];
  int n = 0, i, ipc = -1, id = 1000000;
  long last_press = 0;
  DIR *d = opendir("/dev/input");
  struct dirent *e;
  while (d && (e = readdir(d)) != NULL && n < 8) {
    char path[300];
    int fd;
    if (strncmp(e->d_name, "event", 5) != 0) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
    if (!has_gamepad_keys(fd)) { close(fd); continue; }
    LOGI("player: reading buttons from %s", path);
    pf[n].fd = fd; pf[n].events = POLLIN; n++;
  }
  if (d) closedir(d);
  if (n == 0) LOGW("player: no gamepad evdev device found; buttons won't control mpv");

  while (!p->input_stop) {
    if (n == 0 || poll(pf, (nfds_t)n, 200) <= 0) { if (n == 0) usleep(200 * 1000); goto drain; }
    for (i = 0; i < n; i++) {
      struct input_event ev[16];
      ssize_t r;
      size_t k;
      if (pf[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {   /* device went away: drop it */
        LOGW("player: input device closed (revents 0x%x)", (unsigned)pf[i].revents);
        close(pf[i].fd);
        pf[i--] = pf[--n];
        continue;
      }
      if (!(pf[i].revents & POLLIN)) continue;
      r = read(pf[i].fd, ev, sizeof ev);
      for (k = 0; r > 0 && k < (size_t)r / sizeof ev[0]; k++) {
        const char *cmd = mpv_map_event(ev[k].type, ev[k].code, ev[k].value);
        if (!cmd) continue;
        if (ev[k].type == PP_EV_KEY && !mpv_repeat_ok(&last_press, ev[k].value, mono_ms())) continue;
        if (ipc < 0) ipc = ipc_connect(p->sock);
        if (ipc >= 0 && ipc_send(ipc, cmd, ++id) < 0) { close(ipc); ipc = -1; }
        LOGD("player: button %d/%d -> %s", ev[k].code, ev[k].value, cmd);
      }
    }
  drain:   /* replies to our commands: read and discard so mpv never blocks on us */
    if (ipc >= 0) {
      char junk[1024];
      while (recv(ipc, junk, sizeof junk, MSG_DONTWAIT) > 0) {}
    }
  }
  for (i = 0; i < n; i++) close(pf[i].fd);
  if (ipc >= 0) close(ipc);
  return NULL;
}
#endif

#if defined(PP_PLATFORM_TG5040)
/* Brick Pro pad -> mpv playback commands (reviews/tg-c-evdev-glm.md, input_map.c).
 * Codes: A=304 B=305 X=307 Y=308 L1=310 R1=311 Select=314 Start=315 Menu=316;
 * L2/R2=ABS 2/5, D-pad=ABS 16(X)/17(Y). KEY 59/60 and L3/R3 (317/318) are deliberately
 * unbound. mpv_command_async is thread-safe (called from this evdev thread, not the
 * mpv_handle owner thread). */
static void tg_mpv_send(pp_player *p, const char *cmd, const char *arg) {
  if (!p->mpv) return;
  const char *argv[3] = { cmd, arg, NULL };
  mpv_command_async(p->mpv, 0, argv);
}
static void tg_handle_evdev(pp_player *p, int type, int code, int value) {
  if (type == PP_EV_KEY && value == 2) return;   /* auto-repeat: drop */
  if (type == PP_EV_KEY) {
    switch (code) {
    case 304: tg_mpv_send(p, "cycle", "pause"); return;          /* A -> pause */
    case 305: if (value) tg_mpv_send(p, "seek", "-10"); return;  /* B -> seek back */
    case 307: if (value) tg_mpv_send(p, "seek", "-60"); return;  /* X -> seek back 60 */
    case 308: if (value) tg_mpv_send(p, "seek", "60"); return;   /* Y -> seek 60 */
    case 310: if (value) tg_mpv_send(p, "seek", "-10"); return;  /* L1 -> seek back */
    case 311: if (value) tg_mpv_send(p, "seek", "10"); return;   /* R1 -> seek */
    case 314: tg_mpv_send(p, "cycle", "pause"); return;          /* Select -> pause */
    case 315: tg_mpv_send(p, "cycle", "pause"); return;          /* Start -> pause */
    case 316: tg_mpv_send(p, "quit", NULL); return;             /* Menu -> quit */
    default: break;   /* 317/318 (L3/R3), 59/60 (KEY) deliberately unbound */
    }
  } else if (type == PP_EV_ABS) {
    switch (code) {
    case 16:                                                  /* D-pad X (left/right) */
      if (value < 0) tg_mpv_send(p, "seek", "-10");
      else if (value > 0) tg_mpv_send(p, "seek", "10");
      return;
    case 17:                                                  /* D-pad Y (up/down): sign TBD per reviews MINOR 2 */
      if (value < 0) tg_mpv_send(p, "seek", "60");
      else if (value > 0) tg_mpv_send(p, "seek", "-60");
      return;
    default: break;                                           /* L2/R2 (ABS 2/5) not mapped */
    }
  }
}
static void *tg_input_main(void *arg) {
  pp_player *p = arg;
  struct pollfd pf[8];
  int n = 0, i;
  DIR *d = opendir("/dev/input");
  struct dirent *e;
  while (d && (e = readdir(d)) != NULL && n < 8) {
    char path[300];
    int fd;
    if (strncmp(e->d_name, "event", 5) != 0) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
    if (!has_gamepad_keys(fd)) { close(fd); continue; }
    LOGI("player: tg5040 evdev buttons -> mpv on %s", path);
    pf[n].fd = fd; pf[n].events = POLLIN; n++;
  }
  if (d) closedir(d);
  if (n == 0) LOGW("player: no gamepad evdev device found; buttons won't control mpv");
  while (!p->input_stop) {
    if (poll(pf, (nfds_t)n, 200) <= 0) continue;
    for (i = 0; i < n; i++) {
      if (pf[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        LOGW("player: input device closed (revents 0x%x)", (unsigned)pf[i].revents);
        close(pf[i].fd); pf[i--] = pf[--n]; continue;
      }
      if (!(pf[i].revents & POLLIN)) continue;
      struct input_event ev[16];
      ssize_t r = read(pf[i].fd, ev, sizeof ev);
      for (size_t k = 0; r > 0 && k < (size_t)r / sizeof ev[0]; k++)
        tg_handle_evdev(p, ev[k].type, ev[k].code, ev[k].value);
    }
  }
  for (i = 0; i < n; i++) close(pf[i].fd);
  return NULL;
}
#endif

/* Starts mpv with stdio on /dev/null. 0 = ok, else an errno value.
 * On Linux this is fork + PR_SET_PDEATHSIG + exec, so mpv dies with the app: a crashed app would
 * otherwise leave a fullscreen mpv on the SP that no button can quit (the evdev thread died too).
 * PDEATHSIG fires when the forking *thread* exits, so call player_start from the main/UI thread. */
static int spawn_mpv(pid_t *pid, const char *bin, char **argv) {
#ifdef __linux__
  pid_t parent = getpid(), c;
  int devnull = open("/dev/null", O_RDWR | O_CLOEXEC), e;
  if (devnull < 0) return errno;
  c = fork();
  if (c == 0) {   /* child: async-signal-safe calls only */
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() != parent) _exit(127);   /* the app died before prctl */
    dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2);
    execvp(bin, argv);
    _exit(127);
  }
  e = errno;
  close(devnull);
  if (c < 0) return e;
  *pid = c;
  return 0;
#else
  posix_spawn_file_actions_t fa;
  int rc;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  rc = posix_spawnp(pid, bin, &fa, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  return rc;
#endif
}

pp_player *player_start(const char *url, long start_ms) {
#if defined(PP_PLATFORM_TG5040)
  /* In-process libmpv embed: create the render context BEFORE loadfile (lifetime
   * rule: the GL ctx must be current and the render ctx must exist at file load),
   * bind mpv's video output to our SDL GL window, and never touch the external
   * spawn path below. */
  {
    pp_player *p;
    if (!url || !*url) return NULL;
    if (!(p = calloc(1, sizeof *p))) return NULL;
    p->fd = -1;
    p->pos_ms = start_ms < 0 ? 0 : start_ms;
    p->mpv = mpv_create();
    if (!p->mpv) { LOGE("player: mpv_create failed"); free(p); return NULL; }
    mpv_set_property_string(p->mpv, "idle", "yes");
    mpv_set_property_string(p->mpv, "hwdec", "auto");     /* cedar via avcodec sw fallback */
    mpv_set_property_string(p->mpv, "cache", "yes");
    mpv_set_property_string(p->mpv, "demuxer-max-bytes", "32MiB");
    mpv_set_property_string(p->mpv, "demuxer-max-back-bytes", "16MiB");
    mpv_set_property_string(p->mpv, "network-timeout", "20");
    mpv_set_property_string(p->mpv, "ao", "alsa");       /* device audio via ALSA (ao=alsa) */
    if (mpv_initialize(p->mpv) < 0) {
      LOGE("player: mpv_initialize failed");
      mpv_terminate_destroy(p->mpv);
      free(p);
      return NULL;
    }
    {
      mpv_opengl_init_params glinit = {
        .get_proc_address = tg_gl_get_proc,
        .get_proc_address_ctx = p,
      };
      int advanced = 1;
      mpv_render_param cparams[] = {
        { MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glinit },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced },
        { MPV_RENDER_PARAM_INVALID, NULL },
      };
      if (mpv_render_context_create(&p->rc, p->mpv, cparams) < 0) {
        LOGE("player: mpv_render_context_create failed");
        mpv_terminate_destroy(p->mpv);
        free(p);
        return NULL;
      }
      mpv_render_context_set_update_callback(p->rc, tg_mpv_update, p);
    }
    {
      const char *cmd[] = { "loadfile", url, NULL };
      mpv_command(p->mpv, cmd);
    }
    p->input_stop = 0;
    p->input_running = 0;
    if (pthread_create(&p->input_thread, NULL, tg_input_main, p) == 0) p->input_running = 1;
    else LOGW("player: tg5040 input thread unavailable; buttons won't control mpv");
    LOGI("player: libmpv embed (render API, ao=alsa), start %ld ms", p->pos_ms);
    return p;
  }
#endif
  static int seq;
  char *argv[PP_MPV_MAX_ARGS];
  char bufs[PP_MPV_MAX_ARGS][PP_MPV_ARG_LEN];
  const char *bin = getenv("PP_MPV_BIN"), *vo = getenv("PP_MPV_VO");
  pp_player *p;
  FILE *f;
  int rc;

  if (!url || !*url) return NULL;
  if (!bin || !*bin) bin = "mpv";
#ifndef PP_PLATFORM_DESKTOP
  if (!vo) vo = "sdl";
#endif
  if (!(p = calloc(1, sizeof *p))) return NULL;
  p->fd = -1;
  p->pos_ms = start_ms < 0 ? 0 : start_ms;
  seq++;
  snprintf(p->sock, sizeof p->sock, "/tmp/pocketplex-mpv-%d-%d.sock", (int)getpid(), seq);
  snprintf(p->conf, sizeof p->conf, "/tmp/pocketplex-mpv-%d-%d.conf", (int)getpid(), seq);
  unlink(p->sock);
  if ((f = fopen(p->conf, "w")) != NULL) { fputs(mpv_input_conf, f); fclose(f); }
  else p->conf[0] = 0;

  mpv_build_args(argv, bufs, bin, url, p->pos_ms, p->sock, p->conf[0] ? p->conf : NULL, vo);
#ifdef PP_MPV_EVDEV
  fb_save(p);
#endif
  rc = spawn_mpv(&p->pid, bin, argv);
  if (rc != 0) {
    LOGE("player: can't start %s: %s", bin, strerror(rc));
    if (p->conf[0]) unlink(p->conf);
    free(p);
    return NULL;
  }
  LOGI("player: mpv pid %d, start %ld ms, vo %s", (int)p->pid, p->pos_ms, vo ? vo : "(default)");

#ifdef PP_MPV_EVDEV
  if (pthread_create(&p->input_thread, NULL, input_main, p) == 0) p->input_running = 1;
  else LOGW("player: no input thread; buttons won't control mpv");
#endif
  return p;
}

int player_poll(pp_player *p, long *pos_ms, int *finished) {
#if defined(PP_PLATFORM_TG5040)
  if (!p) {
    if (finished) *finished = 1;
    return -1;
  }
  {
    mpv_event *event;
    double sec = 0.0;
    /* Drain mpv's event queue (non-blocking) for EOF/shutdown / errors. */
    while ((event = mpv_wait_event(p->mpv, 0)) && event->event_id != MPV_EVENT_NONE) {
      if (event->event_id == MPV_EVENT_SHUTDOWN || event->event_id == MPV_EVENT_END_FILE)
        p->mpv_finished = 1;
    }
    if (mpv_get_property(p->mpv, "time-pos", MPV_FORMAT_DOUBLE, &sec) == 0 && sec >= 0.0)
      p->pos_ms = (long)(sec * 1000.0 + 0.5);
    if (pos_ms) *pos_ms = p->pos_ms;
    if (finished) *finished = p->mpv_finished;
    return p->mpv_error ? -1 : 0;
  }
#endif
  double sec;
  int rc;
  if (!p) {
    if (finished) *finished = 1;
    return -1;
  }
  reap(p, 0);
  if (!p->exited) {
    if (p->fd < 0) p->fd = ipc_connect(p->sock);   /* the socket appears ~100 ms after launch */
    if (p->fd >= 0) {
      rc = ipc_time_pos(p, &sec, 300);
      if (rc > 0 && sec >= 0) p->pos_ms = (long)(sec * 1000.0 + 0.5);
      if (rc < 0) { close(p->fd); p->fd = -1; p->rlen = 0; reap(p, 0); }
    }
  }
  if (pos_ms) *pos_ms = p->pos_ms;
  if (finished) *finished = p->exited;
  /* mpv exit codes: 0 = quit/end of file, 4 = quit by signal; 1-3 = init or playback errors. */
  return p->exited && p->exit_code != 0 && p->exit_code != 4 ? -1 : 0;
}

void player_stop(pp_player *p) {
#if defined(PP_PLATFORM_TG5040)
  if (!p) return;
  if (p->input_running) { p->input_stop = 1; pthread_join(p->input_thread, NULL); }
  /* Lifetime rule: free the render context BEFORE terminating mpv. */
  if (p->rc) mpv_render_context_free(p->rc);
  if (p->mpv) mpv_terminate_destroy(p->mpv);
  free(p);
  return;
#endif
  long t0;
  if (!p) return;
#ifdef PP_MPV_EVDEV
  if (p->input_running) { p->input_stop = 1; pthread_join(p->input_thread, NULL); }
#endif
  reap(p, 0);
  if (!p->exited) {
    if (p->fd < 0) p->fd = ipc_connect(p->sock);
    if (p->fd < 0 || ipc_send(p->fd, "[\"quit\"]", ++p->req) < 0) kill(p->pid, SIGTERM);
    for (t0 = mono_ms(); !p->exited && mono_ms() - t0 < 1500; reap(p, 0)) usleep(20 * 1000);
    if (!p->exited) {
      LOGW("player: mpv ignored quit; killing it");
      kill(p->pid, SIGKILL);
      reap(p, 1);
    }
  }
  if (p->fd >= 0) close(p->fd);
  unlink(p->sock);
  if (p->conf[0]) unlink(p->conf);
  LOGI("player: stopped at %ld ms", p->pos_ms);
  free(p);
}

void player_render_frame(pp_player *p) {
#if defined(PP_PLATFORM_TG5040)
  SDL_Window *win = plat_window();
  SDL_GLContext ctx = SDL_GL_GetCurrentContext();
  if (!p || !p->rc) return;
  /* The render context must have the current GL ctx on its thread (us). */
  if (win && ctx) SDL_GL_MakeCurrent(win, ctx);
  p->mpv_update = 0;
  uint64_t flags = mpv_render_context_update(p->rc);
  if (!(flags & MPV_RENDER_UPDATE_FRAME)) return;   /* nothing new to draw */
  if (win) {
    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);
    mpv_opengl_fbo fbo = { .fbo = 0, .w = w, .h = h, .internal_format = 0 };
    mpv_render_param rparams[] = {
      { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
      { MPV_RENDER_PARAM_INVALID, NULL },
    };
    mpv_render_context_render(p->rc, rparams);
    SDL_GL_SwapWindow(win);
  }
#else
  (void)p;
#endif
}
