# src/platform/module.mk.
# platform.h is the core contract; implementations are picked per $(PLATFORM).
# suspend.c and wifi.c are pure of SDL (wifi.c: fork/execv + connmanctl, no-op off
# handhelds) and go in CORE so tests can use them.
CORE_SRCS += src/platform/suspend.c src/platform/wifi.c
APP_SRCS += src/platform/sdl2.c
# tg5040: SDL cannot enumerate the Brick Pro pad, so input comes from raw
# evdev (needs linux/input.h; not compilable off Linux).
ifeq ($(PLATFORM),tg5040)
  APP_SRCS += src/platform/evdev_tg5040.c
endif
