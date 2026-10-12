/* platform/wifi.c: Wi-Fi reconnect after sleep. See wifi.h.
 *
 * Device facts (RG35XX SP / Knulli): connman manages wifi, interface wlan0, radio is
 * rfkill phy0. Tools: /usr/bin/connmanctl, /usr/sbin/iw, /usr/sbin/rfkill.
 *
 * Subprocesses run with fork/execv and a fixed argv — no shell, and the only
 * non-constant argument (the service id) is validated by service_id_ok() before use.
 * Captured output is parsed, never logged raw (it can carry SSIDs/BSSIDs). */
#include "platform/wifi.h"
#include "log.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define CONNMANCTL "/usr/bin/connmanctl"
#define IW         "/usr/sbin/iw"
#define RFKILL     "/usr/sbin/rfkill"

#define WIFI_BUDGET_MS  15000  /* total */
#define WIFI_POLL_MS    3000   /* connectivity re-check after each step */
#define WIFI_POLL_STEP_MS 200

/* ---------------- pure parsers (unit-tested) ---------------- */

int pp_wifi_iw_link_connected(const char *text) {
  if (!text) return -1;
  if (strncmp(text, "Connected to", 12) == 0) return 1;
  if (strncmp(text, "Not connected", 13) == 0) return 0;
  return -1;
}

static int is_hex_lower(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/* ^wifi_[0-9a-f]+_[0-9a-f]*_managed_[a-z0-9]+$ — hand-rolled, no regex lib. */
static int service_id_ok(const char *s) {
  int n = 0;
  if (strncmp(s, "wifi_", 5) != 0) return 0;
  s += 5;
  while (is_hex_lower(*s)) { s++; n++; }         /* mac, 1+ */
  if (n == 0 || *s != '_') return 0;
  s++;
  while (is_hex_lower(*s)) s++;                  /* ssid hex, 0+ */
  if (strncmp(s, "_managed_", 9) != 0) return 0;
  s += 9;
  n = 0;
  while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z')) { s++; n++; }
  return n > 0 && *s == '\0';
}

int pp_wifi_favorite_service(const char *text, char *out, size_t n) {
  if (!text || !out || n == 0) return -1;
  const char *p = text;
  while (*p) {
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    /* the flag column: a token only when the line starts in column 0 */
    int favorite = 0;
    if (len > 0 && p[0] != ' ' && p[0] != '\t') {
      for (size_t i = 0; i < len && p[i] != ' ' && p[i] != '\t'; i++)
        if (p[i] == '*') favorite = 1;
    }
    if (favorite) {
      /* last whitespace token starting with wifi_ */
      char tok[128];
      size_t tl = 0;
      int found = 0;
      size_t i = 0;
      while (i < len) {
        while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
        size_t start = i;
        while (i < len && p[i] != ' ' && p[i] != '\t') i++;
        size_t tl2 = i - start;
        if (tl2 >= 5 && tl2 < sizeof tok && strncmp(p + start, "wifi_", 5) == 0) {
          memcpy(tok, p + start, tl2);
          tok[tl2] = '\0';
          tl = tl2;
          found = 1;
        }
      }
      if (found && service_id_ok(tok)) {
        if (tl + 1 > n) return -1;  /* never truncate into a different id */
        memcpy(out, tok, tl + 1);
        return 0;
      }
    }
    if (!eol) break;
    p = eol + 1;
  }
  return -1;
}

/* Copies one line (bounded) into buf for str*-style parsing. Returns the next line. */
static const char *next_line(const char *p, char *buf, size_t n) {
  const char *eol = strchr(p, '\n');
  size_t len = eol ? (size_t)(eol - p) : strlen(p);
  if (len >= n) len = n - 1;
  memcpy(buf, p, len);
  buf[len] = '\0';
  return eol ? eol + 1 : p + strlen(p);
}

int pp_wifi_rfkill_blocked(const char *text, int *soft, int *hard) {
  if (!text || !soft || !hard) return -1;
  char line[256];
  int in_wifi = 0, have_soft = 0, have_hard = 0;
  while (*text) {
    int header = *text != ' ' && *text != '\t';
    text = next_line(text, line, sizeof line);
    if (header) {
      /* device header, e.g. "0: phy0: Wireless LAN" */
      in_wifi = strstr(line, "Wireless LAN") != NULL;
    } else if (in_wifi) {
      char *s = line;
      while (*s == ' ' || *s == '\t') s++;
      if (strncmp(s, "Soft blocked:", 13) == 0) {
        *soft = strstr(s, "yes") ? 1 : 0;
        have_soft = 1;
      } else if (strncmp(s, "Hard blocked:", 13) == 0) {
        *hard = strstr(s, "yes") ? 1 : 0;
        have_hard = 1;
      }
    }
  }
  return have_soft && have_hard ? 0 : -1;
}

int pp_wifi_connman_state(const char *text, int *powered, int *connected) {
  if (!text || !powered || !connected) return -1;
  char line[256];
  int in_wifi = 0, have_p = 0, have_c = 0;
  while (*text) {
    int header = *text == '/';
    text = next_line(text, line, sizeof line);
    if (header) {
      in_wifi = strncmp(line, "/net/connman/technology/wifi", 28) == 0;
    } else if (in_wifi) {
      char *s = line;
      while (*s == ' ' || *s == '\t') s++;
      char *eq = strchr(s, '=');
      if (!eq) continue;
      char *v = eq + 1;
      while (*v == ' ') v++;
      int val = (*v == 'T' || *v == 't') ? 1 : 0;
      if (strncmp(s, "Powered", 7) == 0)        { *powered = val; have_p = 1; }
      else if (strncmp(s, "Connected", 9) == 0) { *connected = val; have_c = 1; }
    }
  }
  return have_p && have_c ? 0 : -1;
}

/* ---------------- subprocess plumbing (device only) ---------------- */

int pp_wifi_available(void) {
  return access(CONNMANCTL, X_OK) == 0;
}

static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(int ms) {
  struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
  nanosleep(&ts, NULL);
}

/* fork/execv with a fixed argv; captures stdout (capped, NUL-terminated).
 * The child's stderr goes to /dev/null. Returns the exit status, or -1. */
static int run_capture(char *const argv[], char *out, size_t n) {
  int pipefd[2];
  if (out && n > 0) out[0] = '\0';
  if (pipe(pipefd) != 0) return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    int nullfd = open("/dev/null", O_WRONLY);
    dup2(pipefd[1], STDOUT_FILENO);
    if (nullfd >= 0) dup2(nullfd, STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    execv(argv[0], argv);
    _exit(127);
  }
  close(pipefd[1]);
  size_t used = 0;
  for (;;) {
    char buf[512];
    ssize_t r = read(pipefd[0], buf, sizeof buf);
    if (r <= 0) break;
    if (out && used < n - 1) {
      size_t room = n - 1 - used;
      size_t take = (size_t)r < room ? (size_t)r : room;
      memcpy(out + used, buf, take);
      used += take;
    }
  }
  close(pipefd[0]);
  if (out && n > 0) out[used] = '\0';
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) { /* EINTR */ }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int link_connected(void) {
  char buf[1024];
  char *argv[] = { (char *)IW, (char *)"dev", (char *)"wlan0", (char *)"link", NULL };
  if (run_capture(argv, buf, sizeof buf) != 0) return -1;
  return pp_wifi_iw_link_connected(buf);
}

void pp_wifi_log_state(const char *tag) {
  char buf[4096];
  int link = link_connected();
  LOGI("wifi %s: link %s", tag,
       link == 1 ? "connected" : link == 0 ? "not connected" : "unknown");

  char *rargv[] = { (char *)RFKILL, (char *)"list", NULL };
  int soft = -1, hard = -1;
  if (run_capture(rargv, buf, sizeof buf) == 0 && pp_wifi_rfkill_blocked(buf, &soft, &hard) == 0)
    LOGI("wifi %s: rfkill soft=%s hard=%s", tag,
         soft ? "yes" : "no", hard ? "yes" : "no");
  else
    LOGI("wifi %s: rfkill unknown", tag);

  char *cargv[] = { (char *)CONNMANCTL, (char *)"technologies", NULL };
  int powered = -1, connected = -1;
  if (run_capture(cargv, buf, sizeof buf) == 0 &&
      pp_wifi_connman_state(buf, &powered, &connected) == 0)
    LOGI("wifi %s: connman powered=%s connected=%s", tag,
         powered ? "true" : "false", connected ? "true" : "false");
  else
    LOGI("wifi %s: connman unknown", tag);
}

/* Poll the link until it comes up, step's slice (~3 s) or the total budget runs out. */
static int poll_connected(long deadline_ms) {
  long end = now_ms() + WIFI_POLL_MS;
  if (end > deadline_ms) end = deadline_ms;
  while (now_ms() < end) {
    if (link_connected() == 1) return 1;
    sleep_ms(WIFI_POLL_STEP_MS);
  }
  return link_connected() == 1;
}

static void connman(const char *arg) {
  char *argv[] = { (char *)CONNMANCTL, (char *)arg, NULL };
  run_capture(argv, NULL, 0);
}

/* Step (b): connect the favorite service. 1 = connected. */
static int connect_favorite(long deadline_ms) {
  char buf[8192], service[128];
  char *sargv[] = { (char *)CONNMANCTL, (char *)"services", NULL };
  if (run_capture(sargv, buf, sizeof buf) != 0 ||
      pp_wifi_favorite_service(buf, service, sizeof service) != 0) {
    LOGW("wifi: no favorite service found");
    return 0;
  }
  char *argv[] = { (char *)CONNMANCTL, (char *)"connect", service, NULL };
  run_capture(argv, NULL, 0);  /* the id is validated; never logged (pairs with an SSID) */
  return poll_connected(deadline_ms);
}

int pp_wifi_reconnect(void) {
  if (!pp_wifi_available()) return 0;
  long deadline = now_ms() + WIFI_BUDGET_MS;
  int ok = 0;
  pp_wifi_log_state("before");
  if (link_connected() == 1) {
    LOGI("wifi: already connected, nothing to do");
    return 0;
  }
  LOGI("wifi: reconnect step 1: enable wifi");
  connman("enable");
  ok = poll_connected(deadline);
  if (!ok && now_ms() < deadline) {
    LOGI("wifi: reconnect step 2: connect favorite");
    ok = connect_favorite(deadline);
  }
  if (!ok && now_ms() < deadline) {
    LOGI("wifi: reconnect step 3: disable/enable + connect favorite");
    connman("disable");
    connman("enable");
    ok = connect_favorite(deadline);
  }
  pp_wifi_log_state("after");
  LOGI("wifi: reconnect %s", ok ? "succeeded" : "FAILED");
  return ok ? 0 : -1;
}

static int g_reconnect_running;

static void *reconnect_thread(void *arg) {
  (void)arg;
  pp_wifi_reconnect();
  __atomic_store_n(&g_reconnect_running, 0, __ATOMIC_RELEASE);
  return NULL;
}

void pp_wifi_reconnect_async(void) {
  if (!pp_wifi_available()) return;
  int expected = 0;
  if (!__atomic_compare_exchange_n(&g_reconnect_running, &expected, 1,
                                   0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return;  /* one already in flight */
  pthread_t t;
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0 ||
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0 ||
      pthread_create(&t, &attr, reconnect_thread, NULL) != 0) {
    LOGW("wifi: could not start the reconnect thread");
    __atomic_store_n(&g_reconnect_running, 0, __ATOMIC_RELEASE);
  }
  pthread_attr_destroy(&attr);
}
