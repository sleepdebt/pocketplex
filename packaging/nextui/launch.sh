#!/bin/sh
# PocketPlex launcher for the TrimUI Brick Pro (NextUI). Runs from Tools -> PocketPlex.
# The pak folder carries everything it needs: the binary, the libmpv/FFmpeg libraries
# in lib/, the CA bundle, the fonts for the time overlay, and an empty certs/ folder.
cd "$(dirname "$0")"

LOGS="${LOGS_PATH:-/mnt/SDCARD/.userdata/tg5040/logs}"
mkdir -p "${USERDATA_PATH:-$PWD/data}/PocketPlex" "$LOGS"

export POCKETPLEX_INI="${USERDATA_PATH:-$PWD/data}/PocketPlex/pocketplex.ini"
# HTTP client (libcurl with mbedTLS): the bundled CA, and an existing empty CAPATH.
export CURL_CA_BUNDLE="$PWD/cacert.pem"
export PP_CA_PATH="$PWD/certs"
# Fonts for the on-screen time label (see fonts.conf).
export FONTCONFIG_FILE="$PWD/fonts.conf"
# Bundled libmpv and FFmpeg first; NextUI's SDL2 comes from its own folder.
export LD_LIBRARY_PATH="$PWD/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"

./PocketPlex > "$LOGS/PocketPlex.txt" 2>&1
exit $?
