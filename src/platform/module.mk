# src/platform/module.mk.
# platform.h is the core contract; implementations are picked per $(PLATFORM).
# suspend.c is pure (no SDL) and goes in CORE so tests can use it.
CORE_SRCS += src/platform/suspend.c
APP_SRCS += src/platform/sdl2.c
