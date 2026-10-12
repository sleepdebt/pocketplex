# src/platform/module.mk.
# platform.h is the core contract; implementations are picked per $(PLATFORM).
# suspend.c and wifi.c are pure of SDL (wifi.c: fork/execv + connmanctl, no-op off
# handhelds) and go in CORE so tests can use them.
CORE_SRCS += src/platform/suspend.c src/platform/wifi.c
APP_SRCS += src/platform/sdl2.c
