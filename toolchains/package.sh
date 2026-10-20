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
  # TrimUI Brick Pro (NextUI Tool pak). The zip's top level is the pak folder: unzip into
  # SDCARD/Tools/tg5040/ and it shows under Tools -> PocketPlex. The pak is self-contained:
  # libmpv and FFmpeg come from the image (/opt/mpv/lib), copied as real files named by SONAME,
  # because the SD card is exFAT and cannot hold symlinks. The CA bundle is the pinned one from
  # the image (Dockerfile.tg5040). The launcher and fonts.conf live in packaging/nextui/.
  pak="$staging/PocketPlex.pak"
  mkdir -p "$pak/assets" "$pak/lib" "$pak/certs"
  cp "$bin" "$pak/PocketPlex"
  cp -a "$root/assets/." "$pak/assets/" 2>/dev/null || true
  find "$pak/assets" -name .gitkeep -delete 2>/dev/null || true
  cp "$root/packaging/nextui/launch.sh"  "$pak/launch.sh"
  cp "$root/packaging/nextui/fonts.conf" "$pak/fonts.conf"
  cp "${PP_CACERT:-/opt/pp-net/share/pocketplex/cacert.pem}" "$pak/cacert.pem"
  # The NEEDED sonames of PocketPlex (via libmpv). cp -L dereferences the image's symlinks;
  # set -e stops the package if any is missing.
  for n in libmpv.so.2 libass.so.9 libavcodec.so.60 libavfilter.so.9 libavformat.so.60 \
           libavutil.so.58 libswresample.so.4 libswscale.so.7 libfreetype.so.6 \
           libharfbuzz.so.0 libharfbuzz-subset.so.0 libfribidi.so.0 libfontconfig.so.1 \
           libexpat.so.1; do
    cp -L "${PP_MPV_LIBS:-/opt/mpv/lib}/$n" "$pak/lib/$n"
  done
  chmod +x "$pak/launch.sh" "$pak/PocketPlex"
  rm -f "$zipout"
  ( cd "$staging" && zip -qr "$zipout" PocketPlex.pak )
  ;;
*)
  echo "package.sh: unknown platform '$platform' (expected sp|mmp|tg5040)" >&2; exit 2 ;;
esac
echo "packaged: $zipout"
