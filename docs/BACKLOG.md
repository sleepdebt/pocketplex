# Backlog

Minor review findings that didn't block a merge (process §7 step 4). Newest at the bottom. Whoever owns the file picks them up; tick them off when fixed.

## core (review@ cb6b95e, 2026-10-03)
- [ ] `src/plex/http.c:106`: the log line always says "GET". Pass the method through.
- [ ] `src/plex/playback.c:46-51,92-95,105-107`: unchecked `snprintf` truncation for the decision/timeline/scrobble URLs. Return `PP_ERR_ARG` like `pp_build_transcode_url` does.
- [ ] `src/plex/auth.c:149`: every non-2xx PIN poll maps to `PP_ERR_AUTH`. Only 404 should; 429/5xx → `PP_ERR_HTTP`/`PP_ERR_NET`.
- [ ] Paged lists open a new curl handle per page (repeated TLS handshakes). Reuse one handle per call. Worst-case stall is 5 s connect + 20 s total per request, which core's spinner/timeout should account for.
