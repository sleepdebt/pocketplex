# src/player/module.mk. App-only module.
APP_SRCS += src/player/player_mpv.c
# pthread for the evdev input thread. In CORE_LIBS (linked into the app too) because
# tests/test_player_mpv.c #includes player_mpv.c.
CORE_LIBS += -lpthread
