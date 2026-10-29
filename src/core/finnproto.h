#ifndef FINNPROTO_H
#define FINNPROTO_H

/*
 * finnproto — the FinnOS control protocol.
 *
 * Deliberately line-oriented text, not JSON. JSON is needed only at the MCP
 * boundary, where the adapter translates it; keeping the on-device daemons
 * free of a JSON parser keeps them small and keeps a fast-moving external
 * spec out of the OS. It also means any shell can drive the system:
 *
 *     printf 'state.set battery=42\n' | finnctl
 *
 * Socket paths
 *   /run/finn/control.sock   finnd        — the control plane
 *   /run/finn/comp.sock      finn-comp    — display + input provider
 *
 * Requests: one line, whitespace separated, `verb arg=value ...`
 * Replies:  OK
 *           OK <single-line payload>
 *           OK BINARY <len>\n<len raw bytes>
 *           ERR <message>
 * Verb namespaces match the reserved FinnOS naming rules.
 */

#define FINN_PROTO_MAGIC   "FINNCTL/1"
#define FINN_CONTROL_SOCK  "/run/finn/control.sock"
#define FINN_COMP_SOCK     "/run/finn/comp.sock"
#define FINN_STATE_DIR     "/run/finn/state"

/* The in-process xdg-shell server listens here, not under /run/user/<uid>:
 * the compositor runs ProtectHome=read-only, which mounts /run/user
 * read-only in its own namespace, so a bind() there fails with EROFS on
 * every boot. /run/finn is the compositor's own IPC namespace and is
 * writable in its sandbox. WAYLAND_DISPLAY takes an absolute path, so
 * clients connect to exactly this file; no /run/user/<uid>/wayland-0
 * path is used. */
#define FINN_WAYLAND_SOCK  "/run/finn/wayland-0"
#define FINN_BROWSER_SOCK  "/run/finn/browser.sock"

/* The Home panel's recent-activity list. finnd is the only program that
 * starts an app, so it is the only writer; the shell reads it to report what
 * actually happened rather than inventing a history. */
#define FINN_RECENT_FILE   FINN_STATE_DIR "/recent"
#define FINN_RECENT_MAX    12

#define FINN_MAX_LINE      4096

/* Bounds shared by the audited Flatpak protocol and its compositor client. */
#define FINN_FLATPAK_ID_MAX      255
#define FINN_FLATPAK_JOB_ID_MAX   64
#define FINN_FLATPAK_QUERY_MAX   128

/*
 * The Windows catalogue and the runtime OTA share the Flatpak job shape:
 * a validated id, a transient systemd unit, and a word read back from
 * systemd. The bounds are here because finnd and finnpkg are two ends of
 * the same names.
 */
#define FINN_WINGET_ID_MAX       128
#define FINN_WINGET_QUERY_MAX    128
#define FINN_WINGET_JOB_ID_MAX    64
#define FINN_OTA_JOB_ID_MAX       64
#define FINN_WINE_VERSION_MAX     64

/* Finn's per-user Wine data root, relative to the account's home. finnd
 * and finnpkg both name it; the account home is the only variable. */
#define FINN_WINE_DATA_SUFFIX    "/.local/share/finn/wine"

/* finnpkg writes its own job notes here when the directory is writable;
 * the authoritative state is still systemd's. */
#define FINN_WINGET_STATE_DIR    "/run/finn/winget"

#endif
