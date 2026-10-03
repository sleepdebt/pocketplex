/* tests/test.h: minimal unit-test macros. No network in tests.
 *
 *   static void test_parse(void) { CHECK(x == 1); CHECK_STR(a, "b"); }
 *   int main(void) { RUN(test_parse); return TEST_RESULT(); }
 *
 * Each tests/test_*.c is its own binary; `make test` builds and runs them all.
 */
#ifndef PP_TEST_H
#define PP_TEST_H

#include <stdio.h>
#include <string.h>

static int pp_test_failures;
static int pp_test_checks;

#define CHECK(cond) do { \
    pp_test_checks++; \
    if (!(cond)) { pp_test_failures++; \
      fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

#define CHECK_STR(got, want) do { \
    const char *g_ = (got), *w_ = (want); \
    pp_test_checks++; \
    if (!g_ || strcmp(g_, w_) != 0) { pp_test_failures++; \
      fprintf(stderr, "  FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, \
              g_ ? g_ : "(null)", w_); } \
  } while (0)

#define RUN(fn) do { fprintf(stderr, "  %s\n", #fn); fn(); } while (0)

#define TEST_RESULT() \
  (fprintf(stderr, "  %d checks, %d failed\n", pp_test_checks, pp_test_failures), \
   pp_test_failures ? 1 : 0)

#endif /* PP_TEST_H */
