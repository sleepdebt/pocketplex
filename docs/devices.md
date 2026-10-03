# Device notes

SP sections: core. MMP sections: core. Measurements only; each claim has the command that produced it.

---

## Anbernic RG35XX SP

Firmware: Knulli `gladiator-ii` (Batocera base), kernel 4.9.170, glibc 2.40, mpv 0.39.0, ffmpeg 7.1.
Access: `ssh knulli` (shared connection, see the spec). EmulationStation (ES) stays running in the background.

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
