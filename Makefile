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
# OrbStack CLI shim that ships with the OrbStack app.
DOCKER ?= $(shell command -v docker 2>/dev/null)
ifeq ($(DOCKER),)
  _ORBSTACK_DOCKER := /Applications/OrbStack.app/Contents/MacOS/xbin/docker
  ifneq ($(wildcard $(_ORBSTACK_DOCKER)),)
    DOCKER := $(_ORBSTACK_DOCKER)
  endif
endif

DIST ?= dist
# Run containers as the invoking user (no root-owned artifacts in dist/) and build
# into a throwaway dir (/tmp/pp-build) so the host build/ (shared across lanes)
# is never wiped by `make clean` inside the image.
ID_U ?= $(shell id -u)
ID_G ?= $(shell id -g)
.PHONY: docker-sp docker-mmp docker-tg5040 inspect-sp inspect-mmp inspect-tg5040 inspect-libmpv-tg5040
docker-sp:
	@mkdir -p $(DIST)
	$(if $(DOCKER),,$(error No docker found. Install Docker or OrbStack, or put 'docker' on PATH.))
	$(DOCKER) build -t pocketplex:sp -f toolchains/Dockerfile.sp .
	$(DOCKER) run --rm -u $(ID_U):$(ID_G) -v $(CURDIR):/src -w /src pocketplex:sp \
	  sh -c 'make clean BUILD=/tmp/pp-build && make PLATFORM=sp BUILD=/tmp/pp-build \
	  && sh /src/toolchains/package.sh sp /tmp/pp-build/pocketplex dist/pocketplex-sp.zip'
docker-mmp:
	@mkdir -p $(DIST)
	$(if $(DOCKER),,$(error No docker found. Install Docker or OrbStack, or put 'docker' on PATH.))
	$(DOCKER) build -t pocketplex:mmp -f toolchains/Dockerfile.mmp .
	$(DOCKER) run --rm -u $(ID_U):$(ID_G) -v $(CURDIR):/src -w /src pocketplex:mmp \
	  sh -c 'make clean BUILD=/tmp/pp-build && make PLATFORM=mmp BUILD=/tmp/pp-build \
	  && sh /src/toolchains/package.sh mmp /tmp/pp-build/pocketplex dist/pocketplex-mmp.zip'
docker-tg5040:
	@mkdir -p $(DIST)
	$(if $(DOCKER),,$(error No docker found. Install Docker or OrbStack, or put 'docker' on PATH.))
	$(DOCKER) build -t pocketplex:tg5040 -f toolchains/Dockerfile.tg5040 .
	$(DOCKER) run --rm -u $(ID_U):$(ID_G) -v $(CURDIR):/src -w /src pocketplex:tg5040 \
	  sh -c 'make clean BUILD=/tmp/pp-build && make PLATFORM=tg5040 BUILD=/tmp/pp-build >/tmp/pp-make.log 2>&1 \
	  || { cat /tmp/pp-make.log; exit 1; }; \
	  if grep -E "\.c:[0-9]+:[0-9]+: warning:" /tmp/pp-make.log; then echo "FAIL: compiler warnings (gate is 0)"; exit 1; fi \
	  && sh /src/toolchains/package.sh tg5040 /tmp/pp-build/pocketplex dist/pocketplex-tg5040.zip'

# Diagnostic: build in the image and print the real NEEDED list,
# interpreter, and the SDL2/SDL2_ttf/curl versions the cross toolchain sees.
# Read-only on the host (builds into /tmp/i); no device access required.
inspect-sp:
	$(DOCKER) build -t pocketplex:sp -f toolchains/Dockerfile.sp .
	$(DOCKER) run --rm -v $(CURDIR):/src -w /src pocketplex:sp \
	  sh -c 'make clean BUILD=/tmp/i >/dev/null && make PLATFORM=sp BUILD=/tmp/i >/dev/null \
	  && readelf -d /tmp/i/pocketplex | grep NEEDED && \
	  PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig pkg-config --modversion sdl2 SDL2_ttf libcurl && \
	  file /tmp/i/pocketplex'
inspect-mmp:
	$(DOCKER) build -t pocketplex:mmp -f toolchains/Dockerfile.mmp .
	$(DOCKER) run --rm -v $(CURDIR):/src -w /src pocketplex:mmp \
	  sh -c 'make clean BUILD=/tmp/i >/dev/null && make PLATFORM=mmp BUILD=/tmp/i >/dev/null \
	  && readelf -d /tmp/i/pocketplex | grep NEEDED && \
	  PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig pkg-config --modversion sdl2 SDL2_ttf libcurl && \
	  file /tmp/i/pocketplex'
# Like inspect-sp, plus the tg5040 glibc gate: the device runs glibc 2.23, so the
# highest GLIBC_* version symbol the binary imports must be <= 2.23 (the cross
# toolchain's sysroot enforces it; this proves it). Fails the target if newer,
# and also if no GLIBC_* symbols are found at all (a missing binary or a failed
# readelf must not pass the gate vacuously).
inspect-tg5040:
	$(DOCKER) build -t pocketplex:tg5040 -f toolchains/Dockerfile.tg5040 .
	$(DOCKER) run --rm -v $(CURDIR):/src -w /src pocketplex:tg5040 \
	  sh -c 'make clean BUILD=/tmp/i >/dev/null && make PLATFORM=tg5040 BUILD=/tmp/i >/tmp/i-make.log 2>&1 \
	  || { cat /tmp/i-make.log; exit 1; }; \
	  if grep -E "\.c:[0-9]+:[0-9]+: warning:" /tmp/i-make.log; then echo "FAIL: compiler warnings (gate is 0)"; exit 1; fi \
	  && readelf -d /tmp/i/pocketplex | grep NEEDED && \
	  aarch64-nextui-linux-gnu-readelf -l /tmp/i/pocketplex | grep -i interpreter && \
	  max=$$(aarch64-nextui-linux-gnu-readelf --dyn-syms -W /tmp/i/pocketplex | grep -oE "GLIBC_[0-9]+(\.[0-9]+)+" | sed "s/GLIBC_//" | sort -uV | tail -1) && \
	  [ -n "$$max" ] || { echo "FAIL: no GLIBC_ version symbols found (missing binary or failed readelf must not pass the gate)"; exit 1; } && \
	  echo "highest GLIBC_ symbol: GLIBC_$$max (device has 2.23)" && \
	  [ "$$(printf "%s\n2.23\n" "$$max" | sort -V | tail -1)" = "2.23" ] || { echo "FAIL: GLIBC_$$max > 2.23"; exit 1; } && \
	  pkg-config --modversion sdl2 && \
	  file /tmp/i/pocketplex'

# libmpv render-API gate (lane C / ws-c). Rebuild the image (mpv layers are cached
# after the first build) and confirm the libmpv.so built in Dockerfile.tg5040
# exports mpv_render_context_create -- the OpenGL render API entry point paired
# with MPV_RENDER_API_TYPE_OPENGL. Fails if the symbol is missing. Does NOT wire
# libmpv into PocketPlex (no contract-header / package changes).
inspect-libmpv-tg5040:
	$(DOCKER) build -t pocketplex:tg5040 -f toolchains/Dockerfile.tg5040 .
	$(DOCKER) run --rm pocketplex:tg5040 \
	  sh -c 'l=/opt/mpv/lib/libmpv.so; test -e "$$l" \
	  && aarch64-nextui-linux-gnu-nm -D "$$l" | grep -q "mpv_render_context_create" \
	  && aarch64-nextui-linux-gnu-readelf -d "$$l" | grep -q NEEDED \
	  || { echo "FAIL: mpv_render_context_create missing from $$l"; exit 1; }'

-include $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(patsubst %.c,$(OBJ)/%.d,$(TEST_SRCS) $(TOOL_SRCS))
