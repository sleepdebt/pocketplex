# Backlog

Minor review findings that didn't block a merge (process §7 step 4). Newest at the bottom. Whoever owns the file picks them up; tick them off when fixed.

## core (review@ cb6b95e, 2026-10-03)
- [x] `src/plex/http.c:106`: the log line always says "GET". Pass the method through.
- [x] `src/plex/playback.c:46-51,92-95,105-107`: unchecked `snprintf` truncation for the decision/timeline/scrobble URLs. Return `PP_ERR_ARG` like `pp_build_transcode_url` does.
- [x] `src/plex/auth.c:149`: every non-2xx PIN poll maps to `PP_ERR_AUTH`. Only 404 should; 429/5xx → `PP_ERR_HTTP`/`PP_ERR_NET`.
- [ ] Paged lists open a new curl handle per page (repeated TLS handshakes). Reuse one handle per call. (Still open after the merge.) Worst-case stall is 5 s connect + 20 s total per request, which core's spinner/timeout should account for.

## core (review of a feature branch, 2026-10-04)
- [ ] CI: actions/checkout@v4 and actions/upload-artifact@v4 run on deprecated Node 20 (forced to Node 24). Bump when newer majors are available.
- [ ] MMP zip is untested: blocked on core (Onion glibc / libcurl availability; plan a static curl+mbedTLS sysroot).
- [ ] `tools/pp-cli.c:116`: -Wformat-truncation warning in the cross builds.

## core (peer review of a feature branch, 2026-10-04)
- [x] `src/ui/ui.c:181-184`: ui_push at the 16-screen cap leaks the screen object. Destroy and free it instead.
- [x] `src/main.c:112` + `src/ui/ui.c:322`: skip pp_cleanup() when worker_live_count() > 0 after worker_wait_idle times out (exit-path race with curl teardown).
- [ ] `src/platform/sdl2.c:134-136`: plat_suspend_hook never fires on_resume. Hook SDL foreground/resume events during SP bring-up.
- [x] `src/ui/screen_detail.c:96-98`: A on Detail is a toast stub. Wire up player_start + pp_transcode_url (offset 0 + start_ms) with a resume / play-from-start choice.
- [x] `src/ui/fake_provider.c:17-19`: unchecked strdup (smoke/test path).
- [ ] `.clangd` at the repo root: decide and add on main if wanted.
- [ ] MMP zip links libcurl.so.4 dynamically; amended D2 says static curl+mbedTLS on the MMP.
- [ ] -Wformat-truncation in cross builds: src/ui/screen_list.c:114/116, src/ui/screen_detail.c:28 and tools/pp-cli.c:116.
- [ ] Phase 4: subtitle track picker on Detail (contract: subtitleStreamID); today "on" = the track selected in Plex.
- [ ] core: a test that the full decision and start queries match (up to X-Plex-Token); `pp-cli url` should honour cfg.subtitles via _ex; internal.h comment: cite "none" or say "accepted by PMS 1.43.3".
- [ ] `packaging/portmaster/port.json` items lists `pocketplex/PocketPlex`; use `pocketplex` (the directory) so PortMaster uninstall removes the assets too.
- [ ] Config has one token. Saving a shared server's accessToken overwrites the account token (equal for owned servers). Add a separate server_token if shared servers matter.
- [ ] sdl2.c: close GameController/Joystick handles on SDL_JOYDEVICEREMOVED; drop unused g_quit_requested.
- [ ] cJSON.c: 6 sprintf deprecation warnings under clang sanitizer builds.
- [ ] `src/plex/playback.c`: a client id longer than 64 chars makes pp_transcode_url fail (PP_ERR_ARG); skip the param or size the buffer from strlen.
- [ ] `src/ui/screen_detail.c`: Y ("Toggled watched") only flips a local flag; it should call pp_scrobble / an unscrobble on the server. Not documented until it does.
