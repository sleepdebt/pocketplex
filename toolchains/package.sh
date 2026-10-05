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
*)
  echo "package.sh: unknown platform '$platform' (expected sp|mmp)" >&2; exit 2 ;;
esac
echo "packaged: $zipout"
