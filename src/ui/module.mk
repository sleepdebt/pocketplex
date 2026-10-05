# src/ui/module.mk.
# Pure-logic files (no SDL) go in CORE so tests/tools can use them.
# SDL-dependent files are APP-only.
# worker.c uses pthreads (no SDL) so tests can drive it under ASan.
CORE_SRCS += src/ui/list_view.c src/ui/input_map.c src/ui/fake_provider.c src/ui/worker.c src/ui/play_state.c src/ui/ui_stack.c src/ui/session.c
APP_SRCS  += src/ui/ui.c
APP_SRCS  += src/ui/screen_home.c src/ui/screen_list.c src/ui/screen_detail.c
APP_SRCS  += src/ui/screen_servers.c src/ui/screen_link.c src/ui/screen_settings.c src/ui/screen_player.c
APP_LIBS  += -lSDL2_ttf
