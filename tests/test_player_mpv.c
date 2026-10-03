/* tests/test_player_mpv.c: player_mpv.c without a device or network.
 *
 * The implementation is #included so its static helpers can be tested directly. The lifecycle
 * tests point PP_MPV_BIN at this same binary: when it is started with --input-ipc-server=... it
 * acts as a tiny fake mpv (fake_mpv_main) that answers get_property time-pos and quit.
 * test_real_mpv drives the real mpv on a lavfi test source when mpv is installed (skipped otherwise).
 */
#include "player/player_mpv.c"
#include "test.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

/* ---- fake mpv ------------------------------------------------------------------------- */

static double now_s(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

static int fake_mpv_main(int argc, char **argv) {
  const char *sock_path = NULL, *exit_env = getenv("FAKE_MPV_EXIT"), *dur_env = getenv("FAKE_MPV_DURATION");
  const char *log_path = getenv("FAKE_MPV_LOG");
  double start = 0, t0 = now_s(), duration = dur_env ? atof(dur_env) : 1e9;
  int i, srv, clients[8], nclients = 0;
  struct sockaddr_un sa;
  FILE *log = log_path ? fopen(log_path, "w") : NULL;

  for (i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--input-ipc-server=", 19) == 0) sock_path = argv[i] + 19;
    if (strncmp(argv[i], "--start=", 8) == 0) start = atof(argv[i] + 8);
    if (log) fprintf(log, "arg %s\n", argv[i]);
  }
  if (log) fflush(log);
  if (exit_env) return atoi(exit_env);

  srv = socket(AF_UNIX, SOCK_STREAM, 0);
  memset(&sa, 0, sizeof sa);
  sa.sun_family = AF_UNIX;
  snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock_path);
  unlink(sock_path);
  if (srv < 0 || bind(srv, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(srv, 4) < 0) return 1;

  for (;;) {
    struct pollfd pf[9];
    int n = 0;
    if (now_s() - t0 > duration) break;
    pf[n].fd = srv; pf[n].events = POLLIN; n++;
    for (i = 0; i < nclients; i++) { pf[n].fd = clients[i]; pf[n].events = POLLIN; n++; }
    if (poll(pf, n, 50) <= 0) continue;
    if ((pf[0].revents & POLLIN) && nclients < 8) clients[nclients++] = accept(srv, NULL, NULL);
    for (i = 1; i < n; i++) {
      char buf[1024], reply[256];
      ssize_t r;
      if (!(pf[i].revents & (POLLIN | POLLHUP))) continue;
      r = read(pf[i].fd, buf, sizeof buf - 1);
      if (r <= 0) continue;
      buf[r] = 0;
      if (log) { fprintf(log, "cmd %s", buf); fflush(log); }
      if (strstr(buf, "\"quit\"")) { unlink(sock_path); return 0; }
      if (strstr(buf, "time-pos")) {
        const char *rid = strstr(buf, "\"request_id\":");
        snprintf(reply, sizeof reply,
                 "{\"event\":\"playback-restart\"}\n{\"data\":%.6f,\"request_id\":%d,\"error\":\"success\"}\n",
                 start + (now_s() - t0), rid ? atoi(rid + 13) : 0);
        if (write(pf[i].fd, reply, strlen(reply)) < 0) continue;
      }
    }
  }
  unlink(sock_path);
  return 0;
}

/* ---- pure helpers --------------------------------------------------------------------- */

static int has_arg(char **argv, const char *want) {
  for (; *argv; argv++) if (strcmp(*argv, want) == 0) return 1;
  return 0;
}

static void test_build_args(void) {
  char *argv[PP_MPV_MAX_ARGS];
  char bufs[PP_MPV_MAX_ARGS][PP_MPV_ARG_LEN];
  int n = mpv_build_args(argv, bufs, "mpv", "http://h/x.m3u8", 1865250, "/tmp/s.sock", "/tmp/i.conf", "sdl");
  CHECK(n > 4);
  CHECK_STR(argv[0], "mpv");
  CHECK(has_arg(argv, "--no-config"));
  CHECK(has_arg(argv, "--input-ipc-server=/tmp/s.sock"));
  CHECK(has_arg(argv, "--input-conf=/tmp/i.conf"));
  CHECK(has_arg(argv, "--start=1865.250"));
  CHECK(has_arg(argv, "--vo=sdl"));
  CHECK(has_arg(argv, "--fullscreen"));
  CHECK_STR(argv[n - 1], "http://h/x.m3u8");
  CHECK(argv[n] == NULL);

  n = mpv_build_args(argv, bufs, "mpv", "u", 0, "/s", NULL, NULL);
  CHECK(has_arg(argv, "--start=0.000"));
  CHECK(!has_arg(argv, "--vo=sdl"));
  CHECK_STR(argv[n - 1], "u");
}

static void test_parse_reply(void) {
  double v = -1;
  CHECK(mpv_parse_reply("{\"data\":12.345000,\"request_id\":7,\"error\":\"success\"}", 7, &v) == 1);
  CHECK(v > 12.344 && v < 12.346);
  CHECK(mpv_parse_reply("{\"data\":1.0,\"request_id\":8,\"error\":\"success\"}", 7, &v) == 0);
  CHECK(mpv_parse_reply("{\"event\":\"playback-restart\"}", 7, &v) == 0);
  CHECK(mpv_parse_reply("{\"request_id\":7,\"error\":\"property unavailable\"}", 7, &v) == -1);
  CHECK(mpv_parse_reply("{\"data\":1865,\"request_id\":17,\"error\":\"success\"}", 7, &v) == 0);
  CHECK(mpv_parse_reply("", 7, &v) == 0);
}

static void test_button_map(void) {
  CHECK_STR(mpv_map_event(PP_EV_KEY, 304, 1), "[\"cycle\",\"pause\"]");       /* A */
  CHECK(mpv_map_event(PP_EV_KEY, 304, 0) == NULL);                              /* release */
  CHECK(mpv_map_event(PP_EV_KEY, 304, 2) == NULL);                              /* no auto-repeat for pause */
  CHECK_STR(mpv_map_event(PP_EV_KEY, 311, 1), "[\"cycle\",\"pause\"]");       /* Start */
  CHECK_STR(mpv_map_event(PP_EV_KEY, 305, 1), "[\"quit\"]");                  /* B */
  CHECK_STR(mpv_map_event(PP_EV_KEY, 312, 1), "[\"quit\"]");                  /* Menu */
  CHECK_STR(mpv_map_event(PP_EV_ABS, 16, -1), "[\"seek\",-10,\"relative\"]"); /* Left */
  CHECK_STR(mpv_map_event(PP_EV_ABS, 16, 1), "[\"seek\",10,\"relative\"]");   /* Right */
  CHECK(mpv_map_event(PP_EV_ABS, 16, 0) == NULL);                               /* hat centred */
  CHECK_STR(mpv_map_event(PP_EV_ABS, 17, -1), "[\"seek\",300,\"relative\"]"); /* Up */
  CHECK_STR(mpv_map_event(PP_EV_ABS, 17, 1), "[\"seek\",-300,\"relative\"]"); /* Down */
  CHECK_STR(mpv_map_event(PP_EV_KEY, 308, 1), "[\"seek\",-60,\"relative\"]"); /* L1 */
  CHECK_STR(mpv_map_event(PP_EV_KEY, 309, 2), "[\"seek\",60,\"relative\"]");  /* R1 held: repeats */
  CHECK_STR(mpv_map_event(PP_EV_KEY, 307, 1), "[\"show-progress\"]");         /* X */
  CHECK(mpv_map_event(PP_EV_KEY, 115, 1) == NULL);                              /* Vol+ belongs to the OS */
  CHECK(mpv_map_event(2 /* EV_REL */, 0, 1) == NULL);
}

/* ---- lifecycle against the fake ------------------------------------------------------- */

static const char *self_path;

static int poll_until(pp_player *p, long want_pos, double timeout_s, long *pos, int *fin) {
  double t0 = now_s();
  int rc = 0;
  while (now_s() - t0 < timeout_s) {
    rc = player_poll(p, pos, fin);
    if (rc < 0 || *fin || *pos >= want_pos) return rc;
    usleep(50 * 1000);
  }
  return rc;
}

static void test_fake_play_and_stop(void) {
  char logp[] = "/tmp/pp-fake-mpv-XXXXXX";
  int fd = mkstemp(logp);
  long pos = -1;
  int fin = -1, rc;
  double t0;
  pp_player *p;
  FILE *f;
  char line[512];
  int saw_quit = 0;

  if (fd >= 0) close(fd);
  setenv("PP_MPV_BIN", self_path, 1);
  setenv("FAKE_MPV_LOG", logp, 1);
  unsetenv("FAKE_MPV_EXIT"); unsetenv("FAKE_MPV_DURATION");

  p = player_start("http://pms.local/x.m3u8?X-Plex-Token=abc", 5000);
  CHECK(p != NULL);
  if (!p) return;
  rc = player_poll(p, &pos, &fin);   /* immediately: not connected yet, reports start */
  CHECK(rc == 0);
  CHECK(fin == 0);
  CHECK(pos >= 5000);
  rc = poll_until(p, 5200, 3.0, &pos, &fin);
  CHECK(rc == 0);
  CHECK(fin == 0);
  CHECK(pos >= 5200 && pos < 9000);

  t0 = now_s();
  player_stop(p);
  CHECK(now_s() - t0 < 1.5);

  f = fopen(logp, "r");
  CHECK(f != NULL);
  while (f && fgets(line, sizeof line, f)) if (strstr(line, "\"quit\"")) saw_quit = 1;
  if (f) fclose(f);
  CHECK(saw_quit);
  unlink(logp);
  unsetenv("FAKE_MPV_LOG");
}

static void test_fake_ends_by_itself(void) {
  long pos = -1;
  int fin = 0, rc;
  pp_player *p;
  setenv("PP_MPV_BIN", self_path, 1);
  setenv("FAKE_MPV_DURATION", "0.6", 1);
  p = player_start("u", 1000);
  CHECK(p != NULL);
  if (!p) return;
  rc = poll_until(p, 1L << 30, 4.0, &pos, &fin);
  CHECK(rc == 0);
  CHECK(fin == 1);
  CHECK(pos >= 1000);   /* last known position survives the exit */
  player_stop(p);
  unsetenv("FAKE_MPV_DURATION");
}

static void test_fake_fails_to_play(void) {
  long pos = -1;
  int fin = 0, rc;
  pp_player *p;
  setenv("PP_MPV_BIN", self_path, 1);
  setenv("FAKE_MPV_EXIT", "2", 1);    /* mpv: "file could not be played" */
  p = player_start("u", 0);
  CHECK(p != NULL);
  if (!p) return;
  rc = poll_until(p, 1L << 30, 3.0, &pos, &fin);
  CHECK(rc < 0);
  CHECK(fin == 1);
  player_stop(p);
  unsetenv("FAKE_MPV_EXIT");
}

static void test_missing_binary(void) {
  long pos = 0;
  int fin = 0, rc;
  pp_player *p;
  setenv("PP_MPV_BIN", "/nonexistent/mpv", 1);
  p = player_start("u", 0);
  if (p) {   /* spawn may "succeed" and the child then fails to exec: must surface as an error */
    rc = poll_until(p, 1L << 30, 3.0, &pos, &fin);
    CHECK(rc < 0);
    CHECK(fin == 1);
    player_stop(p);
  }
  player_stop(NULL);   /* must be a no-op */
  CHECK(player_poll(NULL, &pos, &fin) < 0);
}

/* ---- real mpv, local lavfi source (no network); skipped when mpv isn't installed ---- */

static void test_real_mpv(void) {
  long pos = -1;
  int fin = 0, rc;
  pp_player *p;
  if (system("command -v mpv >/dev/null 2>&1") != 0) { fprintf(stderr, "    (skipped: no mpv)\n"); return; }
  setenv("PP_MPV_BIN", "mpv", 1);
  setenv("PP_MPV_VO", "null", 1);
  setenv("PP_MPV_AO", "null", 1);
  p = player_start("av://lavfi:testsrc=duration=60:size=64x48:rate=10", 20000);
  CHECK(p != NULL);
  if (!p) return;
  rc = poll_until(p, 20500, 10.0, &pos, &fin);
  CHECK(rc == 0);
  CHECK(fin == 0);
  CHECK(pos >= 20500 && pos < 30000);
  player_stop(p);
  unsetenv("PP_MPV_VO"); unsetenv("PP_MPV_AO");
}

int main(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++)
    if (strncmp(argv[i], "--input-ipc-server=", 19) == 0) return fake_mpv_main(argc, argv);
  self_path = argv[0];
  signal(SIGPIPE, SIG_IGN);
  RUN(test_build_args);
  RUN(test_parse_reply);
  RUN(test_button_map);
  RUN(test_fake_play_and_stop);
  RUN(test_fake_ends_by_itself);
  RUN(test_fake_fails_to_play);
  RUN(test_missing_binary);
  RUN(test_real_mpv);
  return TEST_RESULT();
}
