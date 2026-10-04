## PocketPlex (PortMaster)

A lightweight, text-first Plex client for the RG35XX SP (Knulli).

### Install

In PortMaster: search for "PocketPlex", install, then launch from the Ports menu.

### First run

The app reads `pocketplex.ini` from next to the binary, in the port folder
(`/userdata/roms/ports/pocketplex/pocketplex.ini`; the launcher also exports `POCKETPLEX_INI`
to this path). Copy `pocketplex.ini.example` to `pocketplex.ini` there and fill in your PMS
`server_url` and `token`.
If `token` is empty, PocketPlex will print a 4-digit PIN and you enter it at
**plex.tv/link** on your phone (see the spec).

### PMS settings (required)

- Settings → Network → **Secure connections = "Preferred"** (not "Required"). Plain-HTTP LAN calls
  fail otherwise; if it's "Required" the app must use TLS to `*.plex.direct`.

### Notes

- Video is handled by the system `mpv` (`--vo=sdl`); PocketPlex only launches mpv and reads its
  position over JSON IPC. mpv is therefore not bundled.
- The gamepad is read natively (evdev); `gptokeyb` is not used.
- SDL2/SDL2_ttf/libcurl are linked dynamically against the system copies on the SP.

## Controls

| Button | Action |
|---|---|
| D-pad Up/Down | move / seek ±300 s |
| D-pad Left/Right | seek ±10 s |
| A / Start | pause / resume |
| B / Menu | stop (back to UI) |
| L1 / R1 | seek −60 / +60 s |
| X / Select | show progress |
| Menu (long) | quit app |
