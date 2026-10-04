# toolchains/mmp.mk.  Miyoo Mini Plus (Onion / SigmaStar SSD202D).
#
# Cross-compile for armhf (ARM Cortex-A7, hard-float EABI) from ANY host arch via
# gcc-arm-linux-gnueabihf inside debian:bookworm-slim (toolchains/Dockerfile.mmp).
# bookworm glibc is 2.36; the MMP device glibc must be >= 2.36.
#
# WARNING (see the spec): NOT verified on an actual MMP — no MMP hardware in
# this workspace and core (which characterises the device ABI) has not reported.
# Triple chosen from shauninman/union-miyoomini-toolchain (arm-linux-gnueabihf, hard-float).
# If core shows Onion needs its own sysroot/libc, switch Dockerfile.mmp to
# FROM aemiii91/miyoomini-toolchain (amd64 CI only) and drop the bookworm cross libs.
#
# Linking matches SP: SDL2/SDL2_ttf/libcurl dynamic, cJSON bundled. Onion's launcher
# (src/common/utils/apps.h) cd's into App/PocketPlex and prepends LD_PRELOAD=...libpadsp.so.
ifndef PP_IN_TOOLCHAIN_MMP
PP_IN_TOOLCHAIN_MMP := 1
CC      := arm-linux-gnueabihf-gcc
CXX     := arm-linux-gnueabihf-g++
MMP_PC  := /usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig
SDL_CFLAGS += $(shell PKG_CONFIG_LIBDIR=$(MMP_PC) pkg-config --cflags sdl2 SDL2_ttf libcurl 2>/dev/null)
SDL_LIBS   += $(shell PKG_CONFIG_LIBDIR=$(MMP_PC) pkg-config --libs sdl2 SDL2_ttf 2>/dev/null)
LDFLAGS  += -L/usr/lib/arm-linux-gnueabihf
PP_CFLAGS  += -DPP_PLATFORM_MMP=1
endif
