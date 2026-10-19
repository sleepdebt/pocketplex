/* platform/wifi.h: Wi-Fi reconnect after sleep. Knulli does not reconnect Wi-Fi on
 * its own after suspend, so the resume hook's request retry would have nothing to
 * reach. connmanctl-based (connman manages wifi on the SP); a no-op wherever
 * /usr/bin/connmanctl is absent (desktop, MMP).
 *
 * Logs link/rfkill/connman state before and after, never SSIDs, BSSIDs, keys or IPs.
 * The parsers are pure and unit-tested in tests/test_ui_wifi.c. */
#ifndef PP_WIFI_H
#define PP_WIFI_H

#include <stddef.h>

/* 1 = /usr/bin/connmanctl exists (Linux handheld running connman); 0 elsewhere. */
int pp_wifi_available(void);

/* Builds a fixed connmanctl argv into argv (cap counts slots incl. the terminator):
 * {/usr/bin/connmanctl, op, arg, NULL}. op must be "enable", "disable" or
 * "connect"; arg is required (enable/disable take the technology "wifi"; a bare
 * 'enable' exits 22 on connmanctl). The service id for connect is validated
 * separately (pp_wifi_favorite_service). 0 = ok, -1 = unknown op / missing arg /
 * cap too small. */
int pp_wifi_connman_argv(char *argv[], size_t cap, const char *op, const char *arg);

/* First line of `iw dev wlan0 link`: 1 = "Connected to ...", 0 = "Not connected.",
 * -1 = anything else. */
int pp_wifi_iw_link_connected(const char *text);

/* Contents of /sys/class/net/wlan0/operstate: 1 only when the interface is
 * administratively down ("down", case-insensitive, trailing whitespace ok).
 * "up"/"dormant"/"unknown" are 0: an associated-but-routeless wlan0 must not
 * read as "Wi-Fi is off". */
int pp_wifi_operstate_down(const char *text);

/* 1 when wlan0 is administratively down (Wi-Fi off on NextUI, where connmanctl
 * is absent so the app cannot manage or probe it otherwise). 0 when the state
 * is up/dormant/unknown or cannot be read: never claim "off" without
 * evidence. Pure sysfs read, no subprocess. */
int pp_wifi_interface_down(void);

/* Favorite service out of `connmanctl services` text: a line whose flag column
 * contains '*'; the service id is the last whitespace token starting with wifi_ and
 * must match ^wifi_[0-9a-f]+_[0-9a-f]*_managed_[a-z0-9]+$ (fixed-execv argv safety).
 * 0 = copied to out (NUL-terminated), -1 = none/invalid/too small. */
int pp_wifi_favorite_service(const char *text, char *out, size_t n);

/* Soft/hard block flags of the "Wireless LAN" device in `rfkill list` text.
 * 0 = found (soft/hard outputs are 0 or 1), -1 = not listed. */
int pp_wifi_rfkill_blocked(const char *text, int *soft, int *hard);

/* Powered/Connected of the wifi block in `connmanctl technologies` text.
 * 0 = both found (values 0 or 1), -1 = wifi block missing or incomplete. */
int pp_wifi_connman_state(const char *text, int *powered, int *connected);

/* Log (INFO) link state, rfkill flags and connman Powered/Connected. tag is
 * "before"/"after". Never logs SSIDs, BSSIDs, keys or IPs. */
void pp_wifi_log_state(const char *tag);

/* Reconnect: connmanctl enable wifi -> connect the favorite service ->
 * disable/enable wifi + connect again. Re-checks the link (poll up to ~3 s) after
 * each step and stops at the first success; ~15 s total budget.
 * 0 = connected (or already was), -1 = still down. No-op (0) when unavailable. */
int pp_wifi_reconnect(void);

/* pp_wifi_reconnect() on a detached thread; returns immediately, coalesced (one
 * reconnect in flight at a time). No-op when unavailable. */
void pp_wifi_reconnect_async(void);

/* 1 once no reconnect thread is running (immediately where connmanctl is absent),
 * 0 on timeout with a reconnect still in flight. timeout_ms <= 0 checks once. */
int pp_wifi_reconnect_wait(int timeout_ms);

#endif /* PP_WIFI_H */
