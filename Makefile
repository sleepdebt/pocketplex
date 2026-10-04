# PocketPlex base Makefile. Must stay GNU make 3.81 compatible (macOS /usr/bin/make).
#
#   make                       app + tools for PLATFORM=desktop
#   make test                  build and run every tests/test_*.c (no network)
#   make run                   build and start the desktop app
#   make PLATFORM=sp|mmp       cross builds: settings come from toolchains/<platform>.mk
#
# Lanes don't edit this file. Each module adds its sources in src/<module>/module.mk:
#   CORE_SRCS += ...   no SDL; linked into the app, every test and every tool  (plex, config)
#   APP_SRCS  += ...   app only                                                (ui, platform, player)
#   CORE_LIBS += ...   / APP_LIBS += ...   extra link flags (e.g. -lcurl, -lSDL2_ttf)
#   PP_CFLAGS += ...   extra compile flags
# tests/test_*.c and tools/*.c are picked up automatically (each becomes its own binary).

PLATFORM ?= desktop
BUILD    ?= build
OBJ      := $(BUILD)/obj/$(PLATFORM)
BIN      ?= $(BUILD)/pocketplex

CFLAGS   ?= -O2 -g
LDFLAGS  ?=
PP_CFLAGS := -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE -Isrc -MMD -MP

CORE_SRCS := src/log.c
APP_SRCS  := src/main.c
CORE_LIBS :=
APP_LIBS  :=

ifeq ($(PLATFORM),desktop)
  SDL_CONFIG ?= sdl2-config
  SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
  SDL_LIBS   := $(shell $(SDL_CONFIG) --libs)
  PP_CFLAGS  += -DPP_PLATFORM_DESKTOP=1
else
  PLATFORM_MK := toolchains/$(PLATFORM).mk
  ifeq ($(wildcard $(PLATFORM_MK)),)
    $(error PLATFORM=$(PLATFORM): $(PLATFORM_MK) not found (cross builds are core's))
  endif
  include $(PLATFORM_MK)
endif

include $(wildcard src/*/module.mk)

CORE_OBJS := $(patsubst %.c,$(OBJ)/%.o,$(CORE_SRCS))
APP_OBJS  := $(patsubst %.c,$(OBJ)/%.o,$(APP_SRCS))
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/tests/%,$(TEST_SRCS))
TOOL_SRCS := $(wildcard tools/*.c)
TOOL_BINS := $(patsubst tools/%.c,$(BUILD)/%,$(TOOL_SRCS))

.PHONY: all app tools test run clean
all: app tools
app: $(BIN)
tools: $(TOOL_BINS)

$(BIN): $(APP_OBJS) $(CORE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(APP_LIBS) $(CORE_LIBS) $(SDL_LIBS)

$(TOOL_BINS): $(BUILD)/%: $(OBJ)/tools/%.o $(CORE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(CORE_LIBS)

$(TEST_BINS): $(BUILD)/tests/%: $(OBJ)/tests/%.o $(CORE_OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(CORE_LIBS)

$(OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(PP_CFLAGS) $(SDL_CFLAGS) $(CFLAGS) -c -o $@ $<

# Runs on the host, so use PLATFORM=desktop.
test: $(TEST_BINS)
	@n=0; fail=0; \
	for t in $(TEST_BINS); do \
	  echo "== $$t"; n=$$((n+1)); \
	  if ! ./$$t; then fail=$$((fail+1)); echo "!! $$t FAILED"; fi; \
	done; \
	echo "$$n test binaries, $$fail failed"; \
	[ $$fail -eq 0 ]

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD)

# ---- core: container builds (Docker / OrbStack) --------------------------------
# Auto-detect the Docker CLI: prefer an on-PATH `docker`, otherwise use the
# OrbStack CLI shim that ships with the OrbStack app (the spec).
DOCKER ?= $(shell command -v docker 2>/dev/null)
ifeq ($(DOCKER),)
  _ORBSTACK_DOCKER := /Applications/OrbStack.app/Contents/MacOS/xbin/docker
  ifneq ($(wildcard $(_ORBSTACK_DOCKER)),)
    DOCKER := $(_ORBSTACK_DOCKER)
  endif
endif

DIST ?= dist
.PHONY: docker-sp docker-mmp
docker-sp:
	@mkdir -p $(DIST)
	$(if $(DOCKER),,$(error No docker found. Install Docker or OrbStack, or put 'docker' on PATH.))
	$(DOCKER) build -t pocketplex:sp -f toolchains/Dockerfile.sp .
	$(DOCKER) run --rm -v $(CURDIR):/src -w /src pocketplex:sp \
	  sh -c 'make clean && make PLATFORM=sp && sh /src/toolchains/package.sh sp build/pocketplex dist/pocketplex-sp.zip'
docker-mmp:
	@mkdir -p $(DIST)
	$(if $(DOCKER),,$(error No docker found. Install Docker or OrbStack, or put 'docker' on PATH.))
	$(DOCKER) build -t pocketplex:mmp -f toolchains/Dockerfile.mmp .
	$(DOCKER) run --rm -v $(CURDIR):/src -w /src pocketplex:mmp \
	  sh -c 'make clean && make PLATFORM=mmp && sh /src/toolchains/package.sh mmp build/pocketplex dist/pocketplex-mmp.zip'

-include $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(patsubst %.c,$(OBJ)/%.d,$(TEST_SRCS) $(TOOL_SRCS))
