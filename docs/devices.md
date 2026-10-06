# Device notes

SP sections: core. MMP sections: core. Measurements only; each claim has the command that produced it.

---

## Anbernic RG35XX SP

Firmware: Knulli `gladiator-ii` (Batocera base), kernel 4.9.170, glibc 2.40, mpv 0.39.0, ffmpeg 7.1.
Access: `ssh knulli`. EmulationStation (ES) stays running in the background.

### Playback spike (2026-10-03)

Test stream: a 1080p H.264 movie, transcoded by PMS to HLS with `videoResolution=640x480&maxVideoBitrate=1500`.
PMS delivered 638×346 H.264 23.976 fps + AAC stereo, about 505 kbps. Probe script: mpv with `--input-ipc-server`,
reading properties every 5 s and CPU time from `/proc/<pid>/stat`.

| mpv options | Result |
|---|---|
| `--vo=sdl` | **Works.** `[vo/sdl] Using opengles2` (Mali fbdev). Software decode, `hwdec-current=no`. CPU 65–76% of one core (out of 400%). 0 frame drops, 0 decoder drops, 0 delayed frames over 20 s. `avsync` within ±0.3 ms. Owner confirmed the picture is visible and smooth over ES. mpv RSS about 90 MB. |
| `--vo=gpu` (also `gpu-next`) | **Fails.** `[vo/gpu] Failed initializing any suitable GPU context!` Only `--gpu-context=auto` exists, and there's no DRM (`/dev/dri` missing). Audio only. |
| `--vo=drm` | Not built into this mpv. |
| `--vo=sdl --hwdec=v4l2m2m-copy` | **No hardware decode.** `h264_v4l2m2m: Could not find a valid device`, falls back to software (same numbers as above). |
| `--vo=sdl --input-gamepad=yes` | **Breaks video.** `[vo/sdl] Another component is using SDL already.` then `Video: no video`. So mpv can't read the gamepad itself while drawing with SDL. |
| ffplay | Not installed. |

Decision: **system `/usr/bin/mpv --vo=sdl`, software decode.** No PortMaster mpv or embedded decoder needed.

### Stream position and seeking (important for core / core)

PMS returns a full-length VOD playlist (`#EXT-X-ENDLIST`, 901 segments for a 7202 s movie) with
`#EXT-X-START:TIME-OFFSET=<offset>`. Measured with `--vo=null --ao=null`:

| Transcode URL `offset=` | mpv `--start=` | `time-pos` | Result |
|---|---|---|---|
| 1865 | (none) | 0.8, 2.9, 4.9 … | Plays, but the position is relative to 0, not 1865 |
| 1865 | 1865 | stuck at 1865.0 | **Stalls** (no progress in 12 s) |
| 0 | 1865 | 1865.0 → 1869.4 → 1879.5 | **Works.** Absolute position, first frame about 6–9 s after launch |

So: **build the transcode URL with `offset=0` and pass the resume point to `player_start(url, start_ms)`**.
mpv seeks inside the VOD playlist, and PMS restarts its transcoder at the requested segment.
`copyts=1` changes nothing (mpv still reports 0-based time with a non-zero offset).

Seek latency (`seek <n> relative` over IPC, `--vo=sdl`, time until `time-pos` advances again):

| Seek | Latency |
|---|---|
| startup to first frame | 9.1 s |
| +10 s | 0.26 s |
| −10 s | 0.51 s |
| +300 s | 8.1 s (PMS restarts the transcode) |
| −300 s | 0.51 s |

PMS quirk: `X-Plex-Platform=Linux` makes both `/video/:/transcode/universal/decision` and `start.m3u8`
return **HTTP 400**. `X-Plex-Platform=Chrome` (header and query parameter) returns 200 with
"Direct play not available; Conversion OK".

### 10-minute soak and PMS session lifetime

The soak used `spike.py 600` on the SP with the player's mpv options (`--vo=sdl --hwdec=no --start=1865`, 32/16 MiB
cache), sampled every 30 s.

**Run 1 (no timeline updates, empty `X-Plex-Client-Identifier`): PMS dropped the transcode about 2 minutes in.**
mpv's cache peaked at 275 s ahead (t=120 s), then drained linearly (95 → 65 → 35 → 5.6 s). After that came
`http: HTTP error 404 Not Found` / `hls: Failed to open segment 295` and mpv reported `Exiting... (End of file)`
at t≈400 s, exit code 0. Note that the player reports this as a normal finish (`finished=1`, rc 0) at the last
position. Short tests hide this: they play from the cache.

**Run 2 (client id `pocketplex-sp-test` + `/:/timeline state=playing` every 10 s, like the app will do): clean.**
61 timeline pings were sent. PMS showed the session `throttled: true` (paused, about 6 minutes ahead), and the cache
held steady:

| t (s) | CPU % (1 core) | time-pos | cache ahead (s) | drops (vo/dec/delayed) | avsync (s) |
|---|---|---|---|---|---|
| 30 | 52.3 | 1889.2 | 70 | 0/0/0 | −0.000026 |
| 120 | 69.4 | 1979.2 | 344 | 0/0/0 | −0.000235 |
| 300 | 71.0 | 2159.3 | 345 | 0/0/0 | −0.000021 |
| 450 | 70.4 | 2309.4 | 341 | 0/0/0 | −0.000692 |
| 600 | 70.8 | 2459.4 | 326 | 0/0/0 | −0.000282 |

Every one of the 20 samples had 0 dropped frames, |avsync| ≤ 0.71 ms, and the 600 s of position matched 600 s of wall clock.
No 404s. Not isolated: whether the pings, the non-empty client id, or both keep the session alive. The app sends
both anyway.

`/:/timeline state=stopped time=1936084` set the item's `viewOffset` to exactly 1936084 (then restored to the
owner's 1865674), so `player_poll` positions can be used directly as Plex resume points.
After `transcode/universal/stop?session=…`, `/transcode/sessions` is empty.

### Post-playback glitch in the integrated app (2026-10-05)

Owner report: playback is fine, but after leaving it the UI glitches until the app restarts. Owner's description:
"tearing / half screen", and frames from the video carry over onto the Home and Detail screens.

**Ruled out: fb0 pan/mode.** The first theory was that mpv leaves Mali's double buffer panned to the other half of
the 640×960 virtual framebuffer. `player_mpv.c` now logs fb0 around mpv and restores it if it changed. The
device log from the owner's test with that build (Detail → play → B):

```
15:26:47 player: fb0 before: 640x480 virt 640x960 offset 0,0 bpp 32
15:26:47 player: mpv pid 9388, start 3738239 ms, vo sdl
15:27:58 player: mpv exited (code 0) at 3797377 ms
15:27:58 player: fb0 after mpv: 640x480 virt 640x960 offset 0,0 bpp 32 -> unchanged
```

The glitch still happened, with fb0 identical before and after. The save/restore stays as a cheap guard and log
line (it would undo a pan or mode change if a future firmware's mpv made one), but it isn't the fix.

**Ruled out: partial redraws.** The UI loop (`src/ui/ui.c`, not `no_present`) does `SDL_RenderClear` and a full
redraw before every `plat_present`. So stale video in the UI isn't just undrawn pixels left in the buffer the app
renders into.

**What's known about the two processes:**
- The app and mpv (`--vo=sdl`) each load the system SDL2 2.30.12. Its fbdev backend is SDL's `mali` video driver
  (the string is in `/usr/lib/libSDL2-2.0.so.0`), and both use the GLES2 renderer (mpv logs
  `[vo/sdl] Using opengles2`). So two independent EGL displays and window surfaces exist on one fb0 at the same time,
  and there is no compositor.
- The app keeps its window, renderer and EGL surface alive across playback and only stops presenting
  (`no_present`).
- The standalone harness `pp_play --sdl` (window + renderer created before mpv, one static green frame presented
  after) looked clean to the owner. It differs from the app in `SDL_RENDERER_PRESENTVSYNC` (the app sets it), in
  having no long-lived textures (the app has a glyph/texture cache), and in presenting only once after playback.

**Working hypothesis (not proven):** while mpv's own SDL/EGL instance owns fb0, the app's EGL window surface, its
swap chain, or its GPU-side textures stop matching what the Mali fbdev driver scans out. So the app's presents
partly land on the screen and partly show mpv's last frames. The fix: release the app's SDL window and
renderer before `player_start` and recreate them, and the texture cache, after `player_stop`.

### Final core criterion run in the integrated app (2026-10-05)

Setup: the merged app from `main` on the SP (installed binary sha256 `07273c806fecbcac…`, launched from Ports), test
episode 53835 (*The Americans* S1E2 "The Clock", 1080p HEVC source, 47:48). The owner drove the buttons. core
sampled read-only every 60 s over SSH: mpv properties via `get_property` on the app's IPC socket, `top -b -n1`,
`log.txt`, plus PMS `/transcode/sessions` from the Mac.

The first attempt ran with the owner's `quality = 360p` preset (`maxVideoBitrate=800`). It was stopped after
about 4.5 minutes (clean, 0 drops), and the owner switched Settings → Quality to 480p. The 480p run resumed at
367495 ms (16:24:58), with transcode URL `videoResolution=640x480&maxVideoBitrate=1500&offset=0`.

| t (wall) | time-pos (s) | drops (vo/dec/delayed) | avsync (s) | cache ahead (s) | mpv CPU % / RES | PMS throttled |
|---|---|---|---|---|---|---|
| 16:25:36 | 387.3 | 0/0/0 | 0.0 | 44 | 26.7 / 102 MB | false |
| 16:27:38 | 509.2 | 0/0/0 | 0.0 | 242 | 33.3 / 132 MB | false |
| 16:29:40 | 630.9 | 0/0/0 | 0.0 | 401 | 26.7 / 156 MB | false |
| 16:31:42 | 752.7 | 0/0/0 | 0.0 | 426 | 53.3 / 157 MB | true |
| 16:33:44 | 874.5 | 0/0/0 | 0.0 | 420 | 40.0 / 157 MB | true |
| 16:35:46 | 996.2 | 0/0/0 | 0.0001 | 412 | 33.3 / 157 MB | false |

All 11 samples: 0 dropped, decoder-dropped and delayed frames, and |avsync| ≤ 0.1 ms. Position advanced 608.98 s in about 610 s
of wall clock. The app process itself sat at 50.7 MB RSS and about 0% CPU while mpv played.

| Check | Result |
|---|---|
| 10 min with no visible stutter or A/V drift | **Pass.** About 11 min at 480p settings (plus 4.5 min at 360p). Owner: "Smooth, in sync". Numbers above. |
| Pause, ±10 s seek and exit from buttons | **Pass.** Owner: A pause, A resume, Right, Left, then B all worked, and the Detail screen looked clean after B (no post-playback glitch with core's hand-off fix). Log: `player: mpv exited (code 0) at 1097850 ms`, `play: stopped 53835 at 18:17`. |
| Resume point correct in Plex Web | **Pass.** PMS `viewOffset` = 1097850 (exactly the exit position). Plex Web shows "29 mins left" (47:48 − 18:17 = 29:31). |
| PMS transcode session stopped on exit | **Pass.** `/transcode/sessions` → `[]`, `/status/sessions` → `[]` right after B, and the IPC socket was removed. |
| Plays a **480p** transcode | **Fail.** See below. |
| Menu quits from Home (KEY 312 question) | **Yes.** `input: pad button 5 -> MENU`, then `quit: MENU (button or window close) on Home`. The app exited to ES. |

**The stream is 480×270 at 330 kbps, not 480p.** mpv reported `video-params` 480×270 and `track-list` showed
`hls-bitrate 330000`, even though PMS's `/status/sessions` listed the session as 640×360 at 1377 kbps. Cause:
mpv fetches `start.m3u8` with no `X-Plex-*` headers, only the URL's query string, and the app's URL has no
`X-Plex-Client-Identifier` in the query. Same parameters, fetched without headers (like mpv does):

```
app URL as-is (no client id): BANDWIDTH=330000,RESOLUTION=480x270
+ X-Plex-Client-Identifier : BANDWIDTH=941000,RESOLUTION=640x360
+ client id, product, device: BANDWIDTH=941000,RESOLUTION=640x360
```

So without a client id, PMS ignores `maxVideoBitrate` and serves its lowest rung. The fix belongs in
`pp_transcode_url`: put `X-Plex-Client-Identifier` (and ideally `X-Plex-Product`/`X-Plex-Device`) in the
`start.m3u8` query string. At 640×360 the decode load will rise (the earlier 638×346 spike ran at about 70% of one
core with 0 drops), so the soak needs a rerun after that fix.

### Buttons (evdev)

The controller is `/dev/input/event1` ("Anbernic RG35XX-SP Controller", also `js0`). Measured by reading raw
`input_event`s while the owner pressed each button once:

| Button | Event | Button | Event |
|---|---|---|---|
| A | KEY 304 (BTN_SOUTH) | L1 | KEY 308 (BTN_WEST) |
| B | KEY 305 (BTN_EAST) | R1 | KEY 309 (BTN_Z) |
| X | KEY 307 (BTN_NORTH) | L2 | KEY 314 (BTN_SELECT) |
| Y | KEY 306 (BTN_C) | R2 | KEY 315 (BTN_START) |
| D-pad up/down | ABS 17 (HAT0Y) −1 / +1 | Select | KEY 310 (BTN_TL) |
| D-pad left/right | ABS 16 (HAT0X) −1 / +1 | Start | KEY 311 (BTN_TR) |
| Menu | KEY 312 (BTN_TL2), then KEY 354 | Vol+ / Vol− | KEY 115 / KEY 114 |

Nothing grabs the devices exclusively: ES, `thd` (triggerhappy) and mpv all hold them open at once.
mpv's SDL opens only `event0` (power key) and `event2` ("dierct-keys-polled"), not the controller.

### Player controls (player_mpv.c)

mpv can't read the gamepad itself (see above), so `player_mpv.c` reads evdev in a thread and sends IPC commands:

| Button | Action |
|---|---|
| A, Start | pause / resume |
| B, Menu | stop (back to the UI) |
| Left / Right | seek −10 / +10 s |
| L1 / R1 | seek −60 / +60 s |
| Up / Down | seek +300 / −300 s |
| X, Select | show progress bar |

Held buttons: measured on the SP (16:45), a ~2 s hold of R1 produced only the press (`button 309/1`) and **no
auto-repeat events**. This controller has no evdev key repeat, and the D-pad hat axes never repeat. On controllers
that do repeat (about 30 Hz), L1/R1 repeats are limited to one seek per 500 ms (`PP_MPV_REPEAT_MS`), because every
forward seek past the cache restarts the PMS transcoder. On the SP that limit is just a guard.

**Blocking times (for the UI loop):** `player_start` returns right away (fork/exec). `player_poll` can block up to
**300 ms** waiting for mpv's IPC reply. `player_stop` sends `quit` and waits up to **1.5 s** for mpv to exit, then
SIGKILL plus a blocking reap. In practice it was ~1 s on the SP (log: quit at 16:21:10, exited 16:21:11).
The UI should draw a "Stopping…" frame before calling `player_stop`. All the timeouts use `CLOCK_MONOTONIC`, so NTP
clock steps after Wi-Fi rejoins don't stretch or skip them.

On desktop, mpv's own window takes the keyboard (generated input.conf): Space/Enter pause, ←/→ ±10 s,
PgUp/PgDn ±60 s, ↑/↓ ±300 s, Tab progress, Esc/Backspace/q quit.

mpv is started with `--no-config --no-input-default-bindings --vo=sdl --hwdec=no --start=<resume>`,
plus a 32 MiB forward / 16 MiB back demuxer cache. Its stdout/stderr go to `/dev/null` because mpv prints the
URL, which carries the token. Overrides for testing: `PP_MPV_BIN`, `PP_MPV_VO`, `PP_MPV_AO`.

### Device test harness: `pp_play`

`src/player/pp_play.c` drives `player.h` on the device without the UI. It isn't part of the app build.
Until core's toolchain lands, build it in an arm64 Debian bookworm VM (glibc 2.36, older than the SP's 2.40,
so the binary runs there). Example with OrbStack:

```sh
orb create debian:bookworm pp-build && orb -m pp-build sudo apt-get install -y build-essential libsdl2-dev
orb -m pp-build bash -c 'cd <repo> && mkdir -p build/sp && gcc -std=c99 -Wall -Wextra -O2 -D_DEFAULT_SOURCE \
  -DPP_PLAY_SDL -Isrc $(sdl2-config --cflags) -o build/sp/pp_play src/player/pp_play.c \
  src/player/player_mpv.c src/log.c $(sdl2-config --libs) -lpthread'
scp build/sp/pp_play knulli:/tmp/
# URL from a transcode request with offset=0. Keep it in /tmp (RAM), because it contains the token:
ssh knulli 'PP_DEBUG=1 /tmp/pp_play --sdl 1865000 /tmp/url.txt'
```

**Test from the Ports menu, not over SSH while ES is in its menu.** ES reads the same controller. Over SSH, the
owner's button presses also navigate ES (one test accidentally launched a SNES game behind mpv). When launched
from Ports, ES waits in the background and ignores input. A temporary launcher
`/userdata/roms/ports/PocketPlex Player Test.sh` (device only, not in the repo) runs `pp_play --sdl` that way.

Port-launch run (2026-10-03 15:52, owner watching): blue SDL window → mpv video → buttons → B → green SDL
window → back to ES. The owner confirmed each step. Log (`PP_DEBUG=1`):

```
I pp_play: SDL window up (blue)
I player: mpv pid 10802, start 1865000 ms, vo sdl
I player: reading buttons from /dev/input/event1
D player: button 304/1 -> ["cycle","pause"]
D player: button 304/1 -> ["cycle","pause"]
D player: button 16/1 -> ["seek",10,"relative"]
D player: button 16/-1 -> ["seek",-10,"relative"]
D player: button 309/1 -> ["seek",60,"relative"]
D player: button 305/1 -> ["quit"]
I player: mpv exited (code 0) at 1936084 ms
I pp_play: screen handed back (green) for 5 s
sdl: 18 stale events flushed after player_stop
```

A second port-launch run (16:20) added Up/Down (±300 s) and pause again. All of them logged and acted on, and B
then the green screen and ES followed. The owner looked specifically for ES bleed-through, including after button
presses, and saw **none**.

**SSH-launched tests with ES in the foreground show ES bleed-through.** ES keeps drawing to the same fbdev and
redraws when a button is pressed. During the 10-minute SSH soak, the owner saw the Ports menu behind the video.
That's a test artifact: when port-launched, ES is suspended (verified above). So frame-quality numbers from SSH
runs (the spike tables and the soak below) describe the player (decode, drops, sync), not what the composited
screen looks like.

The 18 stale events are the button presses the app's own SDL queue also received. The UI has to drop them after
`player_stop`.
