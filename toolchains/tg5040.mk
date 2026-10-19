# toolchains/tg5040.mk.  TrimUI Brick Pro / NextUI (tg5040, Allwinner A133P, aarch64).
#
# Built inside toolchains/Dockerfile.tg5040 (FROM the LoveRetro tg5040 toolchain:
# GCC 8.3, TrimUI SDK sysroot with SDL2 2.26 + SDL2_ttf). The cross gcc knows its
# sysroot, so SDK headers/libs are found without extra -I/-L.
#
# pkg-config: the toolchain image exports PKG_CONFIG_PATH + PKG_CONFIG_SYSROOT_DIR
# pointing straight at $SYSROOT/usr (the SDK sysroot is NOT multiarch) - do NOT
# copy SP's PKG_CONFIG_LIBDIR pattern from sp.mk, it points at Debian multiarch
# dirs that don't exist here. The SDK ships no SDL2_ttf.pc either, but SDL2_ttf is
# already linked by name from src/ui/module.mk (APP_LIBS += -lSDL2_ttf), which the
# cross gcc resolves in the sysroot.
#
# libcurl + mbedTLS are STATIC, from /opt/pp-net (built into the image): the device
# has no libcurl, and we don't link its OpenSSL 1.1 / mbedTLS 2.2.1. Static archives
# resolve left-to-right, so curl must come before the mbed* libs and -lpthread after
# (glibc 2.23: pthread_* live in libpthread, not libc); module.mks append -lcurl
# after this file, and the trailing duplicate -lcurl is a no-op once the archive is
# fully pulled in. HTTPS trust comes from CURL_CA_BUNDLE/PP_CA_BUNDLE
# (http.c sets CURLOPT_CAINFO; the pak ships the pinned cacert.pem).
ifndef PP_IN_TOOLCHAIN_TG5040
PP_IN_TOOLCHAIN_TG5040 := 1
CC        := aarch64-nextui-linux-gnu-gcc
TG_PREFIX := /opt/pp-net
SDL_CFLAGS += $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   += $(shell pkg-config --libs sdl2 2>/dev/null)
LDFLAGS    += -L$(TG_PREFIX)/lib
PP_CFLAGS  += -I$(TG_PREFIX)/include -DPP_PLATFORM_TG5040=1
CORE_LIBS  += -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpthread

# libmpv 0.36 (OpenGL render API) is preinstalled at /opt/mpv by
# toolchains/Dockerfile.tg5040 — NOT under the app's TG_PREFIX (/opt/pp-net).
# player_mpv.c embeds it on PP_PLATFORM_TG5040.
TG_MPV     := /opt/mpv
PP_CFLAGS  += -I$(TG_MPV)/include
APP_LIBS   += -lmpv -lGLESv2   # GLESv2: the player loader's glClear/glScissor
# -rpath-link lets the cross ld resolve libmpv's NEEDED deps (libav*, libass,
# ...) from /opt/mpv/lib at link time; -rpath lets the device ld.so find them.
LDFLAGS    += -L$(TG_MPV)/lib -Wl,-rpath,$(TG_MPV)/lib -Wl,-rpath-link,$(TG_MPV)/lib
endif
