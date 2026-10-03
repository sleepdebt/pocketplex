/* log.c: see log.h. */
#include "log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static FILE *g_log_fp;            /* NULL = stderr */
static pp_log_level g_min_level = PP_LOG_INFO;

int pp_log_open(const char *path) {
  pp_log_close();
  if (!path) return 0;
  g_log_fp = fopen(path, "a");
  return g_log_fp ? 0 : -1;
}

void pp_log_close(void) {
  if (g_log_fp) fclose(g_log_fp);
  g_log_fp = NULL;
}

void pp_log_set_level(pp_log_level min_level) { g_min_level = min_level; }

static int is_token_char(char c) {
  return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~';
}

/* Matches "<anything>token" followed by an optional closing quote, '=' or ':', and an optional
 * opening quote; e.g. X-Plex-Token=abc, "authToken":"abc", token = abc. The value is replaced. */
void pp_log_redact(const char *in, char *out, size_t n) {
  size_t o = 0;
  const char *p = in;
  if (!out || n == 0) return;
  if (!in) { out[0] = '\0'; return; }

  while (*p) {
    if (strncasecmp(p, "token", 5) == 0) {
      const char *v = p + 5;
      if (*v == '"') v++;
      while (*v == ' ') v++;
      if (*v == '=' || *v == ':') {
        v++;
        while (*v == ' ') v++;
        if (*v == '"') v++;
        if (is_token_char(*v)) {
          static const char mask[] = "REDACTED";
          const char *m;
          while (p < v && o + 1 < n) out[o++] = *p++;
          for (m = mask; *m && o + 1 < n; m++) out[o++] = *m;
          while (is_token_char(*v)) v++;
          p = v;
          continue;
        }
      }
    }
    if (o + 1 < n) out[o++] = *p;
    p++;
  }
  out[o] = '\0';
}

void pp_log_write(pp_log_level level, const char *file, int line, const char *fmt, ...) {
  static const char tags[] = "DIWE";
  char msg[1024], clean[1024], stamp[32];
  const char *base;
  FILE *fp = g_log_fp ? g_log_fp : stderr;
  time_t now;
  struct tm tm;
  va_list ap;

  if (level < g_min_level) return;

  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  pp_log_redact(msg, clean, sizeof clean);

  now = time(NULL);
  localtime_r(&now, &tm);
  strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
  base = strrchr(file, '/');
  base = base ? base + 1 : file;

  /* One fprintf per line keeps lines whole when the UI and worker threads both log. */
  fprintf(fp, "%s %c %s:%d %s\n", stamp, tags[level], base, line, clean);
  fflush(fp);
}
