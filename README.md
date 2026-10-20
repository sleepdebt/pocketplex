<p align="center"><img src="assets/logo/pocketplexlogo.png" alt="PocketPlex" width="600"></p>

# PocketPlex

Watch your own Plex library on a handheld. PocketPlex is a small text-menu client: your Plex server transcodes to a 480p H.264 stream over home Wi-Fi, and the handheld plays it with mpv. No Plex Pass needed.

PocketPlex runs on two devices:

| Device | Firmware | Status | Install |
|---|---|---|---|
| **Anbernic RG35XX SP** | Knulli | v1 done and verified on the device | [Install on the RG35XX SP](#install-on-the-rg35xx-sp) |
| **TrimUI Brick Pro** | NextUI | Playback, buttons and loader verified on the device; packaging in progress | [Install on the TrimUI Brick Pro](#install-on-the-trimui-brick-pro) |

A Miyoo Mini Plus port is planned, music-first. Not affiliated with Plex Inc. Per-device notes (install, build targets, what works where): [docs/devices.md](docs/devices.md).

**Status:** v1 works on the RG35XX SP: PIN login, server choice, browsing Movies and TV, playback with resume, and watch-progress sync with Plex. On the Brick Pro, PIN login, browsing and playback work on the device; watch-progress reporting is still being verified. See [Status and roadmap](#status-and-roadmap).

---

## What you need

- A Plex Media Server on your home network with enough CPU to transcode (or hardware transcoding). Plex Pass isn't needed.
- On the server: **Settings → Network → Secure connections = "Preferred"** (not "Required").
- Wi-Fi set up on the handheld. Everything else depends on the device you're using (below).

## Install on the RG35XX SP

Needs **Knulli**. PocketPlex uses the system SDL2, SDL2_ttf, libcurl and mpv, which Knulli already ships.

1. Get `pocketplex-sp.zip`, either from the `pocketplex-sp-zip` artifact of the latest green GitHub Actions run, or by building it yourself (see [Building](#building)).
2. Copy the zip's contents to `/userdata/roms/ports/` on the SD card (or over SSH), so you have:
   ```
   /userdata/roms/ports/PocketPlex.sh
   /userdata/roms/ports/pocketplex/   (PocketPlex binary, assets/, port.json, ...)
   ```
3. In EmulationStation, refresh the list once: **Start → Game Settings → Update Gamelists**. PocketPlex then shows up under **Ports**.

To update later, copy the new zip over the old files. Your `pocketplex/pocketplex.ini` (login and settings) survives, because the zip doesn't contain one.

## Install on the TrimUI Brick Pro

Needs **NextUI** (the `tg5040` platform). The package is self-contained: it carries its own libmpv and FFmpeg libraries, so nothing else is needed on the device.

1. Get `pocketplex-tg5040.zip`, from the `pocketplex-tg5040-zip` artifact of a green build, or by building it yourself (see [Building](#building)).
2. Unzip it into `Tools/tg5040/` on the SD card, so you have `Tools/tg5040/PocketPlex.pak/`.
3. In the NextUI launcher, open **Tools → PocketPlex**.

To update later, unzip the new package over the old pak. Your login and settings are in the userdata folder (`.userdata/tg5040/PocketPlex/pocketplex.ini`), not in the pak, so they survive.

Launch PocketPlex from the Tools menu, not over SSH: SSH launches let the launcher steal input and draw over the video.

## First run

1. Open PocketPlex. The **Link** screen shows a 4-character code.
2. On your phone, go to **plex.tv/link** and enter the code. The handheld saves its own login (`pocketplex/pocketplex.ini` on the RG35XX SP, `PocketPlex/pocketplex.ini` in the userdata folder on the Brick Pro).
3. Pick your server from the list. The choice is saved, so later launches open straight on **Home**.

## Using it

**Home** shows Continue Watching and your libraries. Browse Library → Show → Season → Episode (or Movies → Movie) and press A on an item to open its **Detail** screen. If you've watched part of it, A offers **Resume** or **Play from start** (Up/Down to choose).

### Controls on the RG35XX SP

| Button | Menus | During playback |
|---|---|---|
| D-pad Up / Down | Move | Seek +5 / −5 min |
| D-pad Left / Right | — | Seek −10 / +10 s |
| A | Select / Play | Pause / resume |
| B | Back one level | Stop and return to Detail |
| L1 / R1 | Page up / down (on Home, L1 = server list) | Seek −60 / +60 s |
| X | — | Show progress |
| Start | Go to Home | Pause / resume |
| Select | Settings | Show progress |
| Menu | Back; on Home it quits | Stop |

The A/B/X/Y buttons follow the labels printed on the SP. If they seem swapped on another device, use **Settings → Swap A/B**.

### Controls on the TrimUI Brick Pro

The labels on the buttons are the ones that count: B is the bottom button, A the right, X the top, Y the left.

| Button | Menus | During playback |
|---|---|---|
| D-pad Left / Right | — | Seek −10 / +10 s |
| A (right) | Select / Play | Pause / resume |
| B (bottom) | Back one level | Stop and return to Detail |
| X (top) | — | Show or hide the time and progress bar |
| Y (left) | — | — (Mark watched in Detail) |
| Select | — | Show or hide the time and progress bar |
| Start | — | Pause / resume |
| Menu | Quit | Stop and return to Detail |

The D-pad left and right are the only seek controls. Keys F1 and F2 are left unmapped on purpose, so you can bind them to system buttons in NextUI.

**Settings** (Select):
- **Video Quality:** 480p (default, ~1.5 Mbps) or 360p (~0.8 Mbps, for weaker Wi-Fi).
- **Subtitles:** on (default) burns in the subtitle track you've selected for that item in Plex; off sends none.
- **Swap A/B:** swaps confirm and back if your device's layout differs.
- **Sign Out:** clears the saved login, so the next launch shows the Link screen.

Progress is reported to your server while you watch and when you stop, so Plex (and Continue Watching) resume where you left off. On the Brick Pro this reporting is still being verified.

## Troubleshooting

- **PocketPlex isn't in Ports (RG35XX SP):** run **Update Gamelists** in EmulationStation.
- **Can't reach the server / errors after waking the device:** sleep drops Wi-Fi. PocketPlex asks Knulli to rejoin it when the device wakes (you'll see "Reconnecting..."); give it up to ~15 seconds, then go back and retry if it still fails.
- **Low quality or stutter:** check Settings → Video Quality, and that your server can transcode in real time.
- **Playback fails with "loading failed":** check the log. On the Brick Pro, a missing `lib/` folder or a missing `PP_CA_PATH` is the usual cause.
- **Logs:**
  - RG35XX SP: `/userdata/roms/ports/pocketplex/log.txt`
  - Brick Pro: `/mnt/SDCARD/.userdata/tg5040/logs/PocketPlex.txt`

  Both log screen changes, button presses and playback. Tokens are redacted.
- The line `/usr/lib64/libcurl.so.4: no version information available` at startup on the SP is harmless.

## Configuration file

`pocketplex.ini` lives next to the binary on the RG35XX SP, and in the userdata folder on the Brick Pro (the launcher points `POCKETPLEX_INI` at it). You shouldn't have to edit it by hand, but the keys are: `[plex] server_url, token, client_id` and `[ui] quality = 480p|360p, subtitles = burn|off, swap_ab = true|false`. See `pocketplex.ini.example`. **It contains your Plex token, so don't share it or commit it** (it's gitignored).

---

## Building

Desktop (macOS/Linux, needs SDL2, SDL2_ttf and libcurl, e.g. `brew install sdl2 sdl2_ttf`):

```bash
make                 # app + tools for PLATFORM=desktop → build/pocketplex, build/pp-cli
make test            # unit tests (no network)
make run             # run the desktop app (keyboard: arrows, Enter=A, Backspace=B, a/z/x, PgUp/PgDn=L1/R1, s=Start, p=Select, Esc=Menu)
```

Device packages are cross-compiled in Docker (Docker Desktop or OrbStack must be running; only needed for these targets):

```bash
make docker-sp       # → dist/pocketplex-sp.zip        (aarch64, RG35XX SP / Knulli)
make docker-tg5040   # → dist/pocketplex-tg5040.zip    (aarch64, TrimUI Brick Pro / NextUI; glibc ≤ 2.23)
make docker-mmp      # → dist/pocketplex-mmp.zip       (armhf, Miyoo Mini Plus / Onion; untested on the device)
```

The Brick Pro image also builds libmpv 0.36 (OpenGL render API, ALSA audio) and FFmpeg 6.1 with mbedTLS, LGPL v3 (`--enable-version3`). CI (`.github/workflows/build.yml`) builds desktop, SP and MMP on every push and uploads the zips as artifacts.

**Developer tools:**
- `build/pp-cli`: headless client for the real server. `login | servers | ls [key] | ondeck | url <rk> | stop <session> | progress <rk> <ms> | watched <rk> | unwatch <rk>`. It reads `pocketplex.ini` (or `POCKETPLEX_INI`).
- App smoke modes: `--smoke-scroll`, `--smoke-walk` (real server, Library → Episode and back), `--smoke-link` (fake data), `--smoke-play <ratingKey> [--play-seconds N]`, `--exit-after-ms N`.

Merge gate: 0 warnings in `make`, `make test` **and** `make docker-sp` / `make docker-tg5040` (GCC catches warnings macOS clang doesn't). `make inspect-tg5040` checks that the Brick Pro binary needs glibc 2.23 or older.

## Status and roadmap

- **Done (v1, verified on the RG35XX SP):** PIN login, server discovery with friendly names, Movies/TV browsing, transcoded playback (640×480 profile, 0 dropped frames over 10 min), pause/seek/exit from buttons, resume and watch-progress sync, subtitles on/off, Nintendo-layout buttons.
- **Brick Pro (verified on the device, test build):** PIN login, server choice, browsing, full-screen UI, smooth hardware-accelerated video through libmpv with the launcher hand-off, buttons, playback loader.
- **Brick Pro, in progress:** packaging (bundled libraries and launcher in the release zip); watch-progress reporting to Plex.
- **Next (Phase 3):** music mode (Artist → Album → Track, now-playing); Miyoo Mini Plus feasibility spike.
- **Later:** poster thumbnails, on-screen keyboard search, Plex Home users.
- Known follow-ups: [`docs/BACKLOG.md`](docs/BACKLOG.md).

## Project docs

- [`docs/devices.md`](docs/devices.md): per-device summary (Knulli SP, NextUI Brick Pro: install, build target, differences, status) plus measured RG35XX SP findings (player, buttons, display hand-off, soak results).
- [`docs/BACKLOG.md`](docs/BACKLOG.md): minor findings not yet fixed.
