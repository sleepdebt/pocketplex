/* log.h: the one logging macro every workstream uses.
 *
 *   LOGI("loaded %d items from %s", n, key);
 *
 * Lines go to stderr until pp_log_open() points them at a file (pocketplex.log next to the
 * binary on devices). Every line is passed through pp_log_redact(), so values after
 * "...token=" / "...Token": become REDACTED. Still: don't log tokens on purpose.
 */
#ifndef PP_LOG_H
#define PP_LOG_H

#include <stddef.h>

typedef enum { PP_LOG_DEBUG, PP_LOG_INFO, PP_LOG_WARN, PP_LOG_ERROR } pp_log_level;

#if defined(__GNUC__) || defined(__clang__)
#define PP_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define PP_PRINTF(fmt_idx, arg_idx)
#endif

int  pp_log_open(const char *path); /* NULL = stderr. 0 = ok, <0 = couldn't open (stays on stderr) */
void pp_log_close(void);
void pp_log_set_level(pp_log_level min_level);
void pp_log_write(pp_log_level level, const char *file, int line, const char *fmt, ...) PP_PRINTF(4, 5);

/* Copies in to out (size n, always NUL-terminated), replacing token values with REDACTED. */
void pp_log_redact(const char *in, char *out, size_t n);

#define PP_LOG(level, ...) pp_log_write((level), __FILE__, __LINE__, __VA_ARGS__)
#define LOGD(...) PP_LOG(PP_LOG_DEBUG, __VA_ARGS__)
#define LOGI(...) PP_LOG(PP_LOG_INFO, __VA_ARGS__)
#define LOGW(...) PP_LOG(PP_LOG_WARN, __VA_ARGS__)
#define LOGE(...) PP_LOG(PP_LOG_ERROR, __VA_ARGS__)

#endif /* PP_LOG_H */
