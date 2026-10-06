## PocketPlex (RG35XX SP / Knulli)

A lightweight, text-first Plex client. It streams from your own Plex Media Server over home Wi-Fi.
Full documentation: the project README (install, controls, settings, troubleshooting).

### Install

PocketPlex isn't in the PortMaster catalogue. Install it manually:

1. Copy this package's contents to `/userdata/roms/ports/`, giving you `PocketPlex.sh` and the `pocketplex/` folder.
2. In EmulationStation: **Start → Game Settings → Update Gamelists**. The entry won't appear under
   **Ports** until ES rescans `/userdata/roms/ports`.

Updating: copy the new files over the old ones. Your `pocketplex/pocketplex.ini` (login and settings)
is kept, because the package doesn't contain one.

### First run

Launch **Ports → PocketPlex**. The Link screen shows a 4-character code: enter it at **plex.tv/link**
on your phone. Then pick your server (listed by name). Both are saved to
`/userdata/roms/ports/pocketplex/pocketplex.ini` (the launcher exports `POCKETPLEX_INI` to that path),
so later launches open on Home.

Advanced: you can also pre-fill `server_url` and `token` in that file (see `pocketplex.ini.example`
in the project) to skip linking. The file holds your Plex token, so keep it private.

### Server settings (required)

- Plex Media Server → Settings → Network → **Secure connections = "Preferred"** (not "Required").
- The server must be able to transcode in real time (or use hardware transcoding). Plex Pass isn't needed.

### Controls

A select / pause · B back / stop playback · D-pad move, Left/Right ±10 s and Up/Down ±5 min in
playback · L1/R1 page, or ±60 s in playback · Start Home · Select Settings · Menu quits (from Home).
The buttons follow the printed A/B/X/Y labels. Change this under Settings → Swap A/B if needed.

### Notes

- Logs: `/userdata/roms/ports/pocketplex/log.txt` (tokens are redacted).
- `libcurl.so.4: no version information available` at startup is harmless.
- PocketPlex reads the gamepad itself (no gptokeyb). Playback uses the system `mpv`.
