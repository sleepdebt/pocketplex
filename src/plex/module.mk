# src/plex/module.mk. Core module: no SDL, linked into app, tests and tools.
CORE_SRCS += src/plex/http.c src/plex/auth.c src/plex/library.c src/plex/playback.c
CORE_SRCS += src/plex/cJSON.c
CORE_LIBS += -lcurl
