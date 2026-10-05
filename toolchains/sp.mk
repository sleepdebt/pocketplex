# toolchains/sp.mk.  RG35XX SP (Knulli "gladiator-ii").
#
# Cross-compile for aarch64 from ANY host arch via gcc-aarch64-linux-gnu inside the
# debian:bookworm-slim image (toolchains/Dockerfile.sp). bookworm glibc is 2.36, the
# SP's is 2.40 => forward compatible (see docs/devices.md "SP access").
#
# Linking (the spec D2/D6): SDL2/SDL2_ttf/libcurl link dynamically and resolve against
# the device's system libs at runtime (soname-compatible; validated by core's pp_play).
# cJSON is bundled (src/plex/cJSON.c via src/plex/module.mk). No gptokeyb: gamepads
# are read natively via evdev (player_mpv) and SDL.
#
# pkg-config is called with PKG_CONFIG_LIBDIR set inline (make's `export` doesn't reach
# $(shell) reliably on all make versions), targeting the arm64 multiarch .pc dir.
ifndef PP_IN_TOOLCHAIN_SP
PP_IN_TOOLCHAIN_SP := 1
CC      := aarch64-linux-gnu-gcc
CXX     := aarch64-linux-gnu-g++
SP_PC   := /usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
SDL_CFLAGS += $(shell PKG_CONFIG_LIBDIR=$(SP_PC) pkg-config --cflags sdl2 SDL2_ttf libcurl 2>/dev/null)
SDL_LIBS   += $(shell PKG_CONFIG_LIBDIR=$(SP_PC) pkg-config --libs sdl2 SDL2_ttf 2>/dev/null)
LDFLAGS  += -L/usr/lib/aarch64-linux-gnu
PP_CFLAGS  += -DPP_PLATFORM_SP=1
endif
