## PocketPlex for Onion (Miyoo Mini Plus)

Drop the `App/PocketPlex/` folder from the zip onto the root of the SD card
(`/mnt/SDCARD/App/PocketPlex/`). It appears in the Apps menu on next boot.

### First run

The app reads `pocketplex.ini` from `./conf/`. Copy `pocketplex.ini.example`
next to `launch.sh` and set your PMS `server_url` and `token` (or leave `token`
blank and use the PIN flow at plex.tv/link).

### PMS settings (required)

- Settings → Network → **Secure connections = "Preferred"** (not "Required").

### Notes

- Onion's launcher preloads `libpadsp.so` (OSS audio shim); this helps SDL audio
  and any child `mpv` for the v1 playback path.
- MMP build: `make docker-mmp` cross-compiles with `arm-linux-gnueabihf`
  (hard-float ARMv7, bookworm glibc). Device glibc must be >= the build glibc;
  verify with core before release.
