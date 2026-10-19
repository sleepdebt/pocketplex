#!/bin/bash
# toolchains/package.sh.
# Assembles a device release zip from a built binary + assets + a packaging template.
#   usage: package.sh <platform> <binary> <zip_out>
# Run inside the build container (repo is mounted at /src). No network.
set -e
platform=$1; bin=$2; zipout=$3
cd /src
root=/src
# Absolutise the zip path (the zip is created after `cd $staging` below).
case "$zipout" in /*) ;; *) zipout="$PWD/$zipout" ;; esac
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT

case "$platform" in
sp)
  # PortMaster port layout (see packaging/portmaster and devices.md "SP access").
  # ES discovers ports from /userdata/roms/ports/*.sh; the launcher is PocketPlex.sh
  # and the binary folder is /userdata/roms/ports/pocketplex/.
  mkdir -p "$staging/pocketplex/assets"
  cp "$bin" "$staging/pocketplex/PocketPlex"
  # Preserve the whole assets/ tree (assets/font/...) into pocketplex/assets/.
  cp -a "$root/assets/." "$staging/pocketplex/assets/" 2>/dev/null || true
  find "$staging/pocketplex/assets" -name .gitkeep -delete 2>/dev/null || true
  # ES discovers ports from /userdata/roms/ports/*.sh (top level); the binary and
  # PortMaster metadata live in the sibling pocketplex/ folder (see netsurf on device).
  cp "$root/packaging/portmaster/PocketPlex.sh"  "$staging/PocketPlex.sh"
  cp "$root/packaging/portmaster/port.json"      "$staging/pocketplex/port.json"
  cp "$root/packaging/portmaster/gameinfo.xml"   "$staging/pocketplex/gameinfo.xml"
  cp "$root/packaging/portmaster/README.md"      "$staging/pocketplex/README.md"
  chmod +x "$staging/PocketPlex.sh" "$staging/pocketplex/PocketPlex"
  rm -f "$zipout"
  ( cd "$staging" && zip -qr "$zipout" PocketPlex.sh pocketplex )
  ;;
mmp)
  # Onion app layout (see packaging/onion and docs/devices.md "MMP").
  # Onion reads /mnt/SDCARD/App/<dir>/config.json (flat "key: value"), runs "launch".
  mkdir -p "$staging/App/PocketPlex/assets"
  cp "$bin" "$staging/App/PocketPlex/PocketPlex"
  cp -a "$root/assets/." "$staging/App/PocketPlex/assets/" 2>/dev/null || true
  find "$staging/App/PocketPlex/assets" -name .gitkeep -delete 2>/dev/null || true
  cp "$root/packaging/onion/launch.sh"   "$staging/App/PocketPlex/launch.sh"
  cp "$root/packaging/onion/config.json"  "$staging/App/PocketPlex/config.json"
  cp "$root/packaging/onion/README.md"    "$staging/App/PocketPlex/README.md" 2>/dev/null || true
  chmod +x "$staging/App/PocketPlex/launch.sh" "$staging/App/PocketPlex/PocketPlex"
  rm -f "$zipout"
  ( cd "$staging" && zip -qr "$zipout" App )
  ;;
tg5040)
  # NextUI Tool pak (see BRICKPRO_HANDOFF §5 ws-d). The zip's top level is the
  # pak folder itself: unzip into SDCARD/Tools/tg5040/ and it shows under
  # Tools -> PocketPlex. No mpv bundle yet (tg-b pending), so this pak is the
  # interim -nompv one: binary + assets + pinned CA bundle + a minimal launcher.
  # ws-d owns packaging/nextui/*: once its launch.sh/README land, they win.
  mkdir -p "$staging/PocketPlex.pak/assets"
  cp "$bin" "$staging/PocketPlex.pak/PocketPlex"
  cp -a "$root/assets/." "$staging/PocketPlex.pak/assets/" 2>/dev/null || true
  find "$staging/PocketPlex.pak/assets" -name .gitkeep -delete 2>/dev/null || true
  if [ -f "$root/packaging/nextui/launch.sh" ]; then
    cp "$root/packaging/nextui/launch.sh" "$staging/PocketPlex.pak/launch.sh"
  else
    # Interim launcher until ws-d's packaging/nextui/launch.sh lands. Same env
    # contract the real one must keep: ini in userdata, CA bundle in the pak.
    cat > "$staging/PocketPlex.pak/launch.sh" <<'EOS'
#!/bin/sh
# Interim PocketPlex launcher (tg-a toolchain lane). packaging/nextui/launch.sh
# (ws-d) replaces this when it lands: CPU governor, syncsettings.elf, logging.
cd "$(dirname "$0")"
mkdir -p "${USERDATA_PATH:-$PWD/data}/PocketPlex"
export POCKETPLEX_INI="${USERDATA_PATH:-$PWD/data}/PocketPlex/pocketplex.ini"
export CURL_CA_BUNDLE="$PWD/cacert.pem"
./PocketPlex
exit $?
EOS
  fi
  # Pinned CA bundle built into the image (Dockerfile.tg5040, cacert-2026-09-25).
  cp "${PP_CACERT:-/opt/pp-net/share/pocketplex/cacert.pem}" \
     "$staging/PocketPlex.pak/cacert.pem"
  chmod +x "$staging/PocketPlex.pak/launch.sh" "$staging/PocketPlex.pak/PocketPlex"
  rm -f "$zipout"
  ( cd "$staging" && zip -qr "$zipout" PocketPlex.pak )
  ;;
*)
  echo "package.sh: unknown platform '$platform' (expected sp|mmp|tg5040)" >&2; exit 2 ;;
esac
echo "packaged: $zipout"
