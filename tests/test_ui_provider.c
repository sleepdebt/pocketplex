#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "ui/fake_provider.h"

static void test_fake_servers(void) {
  pp_server *servers = NULL;
  int count = 0;
  CHECK(fake_servers(&servers, &count) == 0);
  CHECK(count >= 1);
  CHECK(servers[0].url != NULL);
  CHECK(servers[0].client_id != NULL);
  CHECK_STR(servers[0].name, "Desktop (fake)");
  pp_servers_free(servers, count);  /* frees name too */
}

static void test_fake_sections_count(void) {
  pp_list out;
  memset(&out, 0, sizeof(out));
  CHECK(fake_sections(NULL, &out) == 0);
  CHECK(out.count >= 3); /* Movies, TV Shows, Music at minimum */
  CHECK(out.items != NULL);
  pp_list_free(&out);
}

static void test_fake_tv_library_2000_items(void) {
  pp_list out;
  memset(&out, 0, sizeof(out));
  CHECK(fake_children(NULL, "tv", &out) == 0);
  CHECK(out.count == 2000);
  /* Verify first and last items have expected fields */
  CHECK(out.items[0].kind == PP_EPISODE);
  CHECK(out.items[0].title[0] != 0);
  CHECK(out.items[1999].kind == PP_EPISODE);
  CHECK(out.items[1999].title[0] != 0);
  pp_list_free(&out);
}

static void test_fake_movies_library_50_items(void) {
  pp_list out;
  memset(&out, 0, sizeof(out));
  CHECK(fake_children(NULL, "movies", &out) == 0);
  CHECK(out.count == 50);
  CHECK(out.items[0].kind == PP_MOVIE);
  CHECK(out.items[0].year > 1900);
  CHECK(out.items[0].duration_ms > 0);
  pp_list_free(&out);
}

static void test_fake_on_deck_has_resume(void) {
  pp_list out;
  memset(&out, 0, sizeof(out));
  CHECK(fake_on_deck(NULL, &out) == 0);
  CHECK(out.count > 0);
  int has_resume = 0;
  int i;
  for (i = 0; i < out.count; i++) {
    if (out.items[i].view_offset_ms > 0) has_resume = 1;
  }
  CHECK(has_resume);
  pp_list_free(&out);
}

static void test_fake_item_detail(void) {
  const pp_item *it = fake_item(NULL, "movie_Neon Horizon");
  CHECK(it != NULL);
  CHECK(it->kind == PP_MOVIE);
  CHECK(it->summary[0] != 0);
  CHECK(it->duration_ms > 0);

  it = fake_item(NULL, "ep_1_5");
  CHECK(it != NULL);
  CHECK(it->kind == PP_EPISODE);
  CHECK(it->title[0] != 0);
}

static void test_fake_settings_defaults(void) {
  pp_settings *s = fake_settings();
  CHECK(s != NULL);
  CHECK(s->signed_in == 1);
  CHECK(s->quality_480p == 1);
}

int main(void) {
  RUN(test_fake_servers);
  RUN(test_fake_sections_count);
  RUN(test_fake_tv_library_2000_items);
  RUN(test_fake_movies_library_50_items);
  RUN(test_fake_on_deck_has_resume);
  RUN(test_fake_item_detail);
  RUN(test_fake_settings_defaults);
  return TEST_RESULT();
}
