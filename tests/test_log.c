#include "log.h"
#include "test.h"

static const char *redact(const char *in) {
  static char out[256];
  pp_log_redact(in, out, sizeof out);
  return out;
}

static void test_redacts_query_param(void) {
  CHECK_STR(redact("GET http://10.0.0.2:32400/x?a=1&X-Plex-Token=abcDEF123_-&b=2"),
            "GET http://10.0.0.2:32400/x?a=1&X-Plex-Token=REDACTED&b=2");
}

static void test_redacts_header_and_json(void) {
  CHECK_STR(redact("X-Plex-Token: abc123"), "X-Plex-Token: REDACTED");
  CHECK_STR(redact("{\"authToken\":\"abc123\",\"id\":7}"), "{\"authToken\":\"REDACTED\",\"id\":7}");
  CHECK_STR(redact("{\"accessToken\": \"zz9\"}"), "{\"accessToken\": \"REDACTED\"}");
  CHECK_STR(redact("token = s3cr3t"), "token = REDACTED");
}

static void test_leaves_other_text_alone(void) {
  CHECK_STR(redact("tokens: 3, no secrets here"), "tokens: 3, no secrets here");
  CHECK_STR(redact("token"), "token");
  CHECK_STR(redact(""), "");
}

static void test_truncates_safely(void) {
  char out[8];
  pp_log_redact("X-Plex-Token=abcdef", out, sizeof out);
  CHECK_STR(out, "X-Plex-");
  pp_log_redact(NULL, out, sizeof out);
  CHECK_STR(out, "");
}

int main(void) {
  RUN(test_redacts_query_param);
  RUN(test_redacts_header_and_json);
  RUN(test_leaves_other_text_alone);
  RUN(test_truncates_safely);
  return TEST_RESULT();
}
