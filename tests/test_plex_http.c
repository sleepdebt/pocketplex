/* tests/test_plex_http.c: env handling in http.c, no network.
 *
 * The tg5040 pak has no system trust store, so http.c points libcurl at the
 * pak's pinned cacert.pem when CURL_CA_BUNDLE (libcurl's own env convention)
 * or PP_CA_BUNDLE is set. These tests drive the pure helper directly.
 */
#include "plex/internal.h"
#include "test.h"
#include <stdlib.h>

static void clear_env(void) {
  unsetenv("CURL_CA_BUNDLE");
  unsetenv("PP_CA_BUNDLE");
}

static void test_ca_bundle_unset(void) {
  char out[512];
  clear_env();
  CHECK(pp_http_ca_bundle(out, sizeof out) == 0);
  CHECK(out[0] == '\0');
}

static void test_ca_bundle_curl_env(void) {
  char out[512];
  clear_env();
  setenv("CURL_CA_BUNDLE", "/tmp/pak/cacert.pem", 1);
  CHECK(pp_http_ca_bundle(out, sizeof out) == 1);
  CHECK_STR(out, "/tmp/pak/cacert.pem");
}

static void test_ca_bundle_pp_env(void) {
  char out[512];
  clear_env();
  setenv("PP_CA_BUNDLE", "/sd/Tools/tg5040/PocketPlex.pak/cacert.pem", 1);
  CHECK(pp_http_ca_bundle(out, sizeof out) == 1);
  CHECK_STR(out, "/sd/Tools/tg5040/PocketPlex.pak/cacert.pem");
}

static void test_ca_bundle_curl_wins_over_pp(void) {
  char out[512];
  clear_env();
  setenv("CURL_CA_BUNDLE", "/a/cacert.pem", 1);
  setenv("PP_CA_BUNDLE", "/b/cacert.pem", 1);
  CHECK(pp_http_ca_bundle(out, sizeof out) == 1);
  CHECK_STR(out, "/a/cacert.pem");
}

static void test_ca_bundle_empty_is_unset(void) {
  char out[512];
  clear_env();
  setenv("CURL_CA_BUNDLE", "", 1);
  setenv("PP_CA_BUNDLE", "", 1);
  CHECK(pp_http_ca_bundle(out, sizeof out) == 0);
}

static void test_ca_bundle_pp_used_when_curl_empty(void) {
  char out[512];
  clear_env();
  setenv("CURL_CA_BUNDLE", "", 1);
  setenv("PP_CA_BUNDLE", "/c/cacert.pem", 1);
  CHECK(pp_http_ca_bundle(out, sizeof out) == 1);
  CHECK_STR(out, "/c/cacert.pem");
}

static void test_ca_bundle_truncated_rejected(void) {
  char out[8];
  clear_env();
  setenv("CURL_CA_BUNDLE", "/very/long/path/cacert.pem", 1);
  /* A truncated path would point at the wrong file: must report unset. */
  CHECK(pp_http_ca_bundle(out, sizeof out) == 0);
  CHECK(out[0] == '\0');
}

static void test_ca_bundle_bad_args(void) {
  clear_env();
  setenv("CURL_CA_BUNDLE", "/cacert.pem", 1);
  CHECK(pp_http_ca_bundle(NULL, 512) == 0);
  CHECK(pp_http_ca_bundle(NULL, 0) == 0);
}

int main(void) {
  RUN(test_ca_bundle_unset);
  RUN(test_ca_bundle_curl_env);
  RUN(test_ca_bundle_pp_env);
  RUN(test_ca_bundle_curl_wins_over_pp);
  RUN(test_ca_bundle_empty_is_unset);
  RUN(test_ca_bundle_pp_used_when_curl_empty);
  RUN(test_ca_bundle_truncated_rejected);
  RUN(test_ca_bundle_bad_args);
  clear_env();
  return TEST_RESULT();
}
