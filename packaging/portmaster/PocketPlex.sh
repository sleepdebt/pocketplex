#!/bin/bash
# PocketPlex.sh — PortMaster launcher for the RG35XX SP (Knulli).
# PocketPlex reads the gamepad natively via evdev (player_mpv) and SDL, so it does
# NOT use gptokeyb. See the spec core and docs/devices.md "SP access".
set -e

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

# PocketPlex reads the SP controller directly; no gptokeyb. Still pull the
# SDL controller db so the SDL UI layer recognises the gamepad.
get_controls

GAMEDIR=/$directory/ports/pocketplex
CONFDIR="$GAMEDIR/conf"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

mkdir -p "$CONFDIR"
cd "$GAMEDIR"

export XDG_DATA_HOME="$CONFDIR"
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Hand this terminal to mpv during playback; it draws to the same fbdev.
./PocketPlex

pm_finish
