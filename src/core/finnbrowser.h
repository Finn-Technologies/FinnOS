#ifndef FINNBROWSER_H
#define FINNBROWSER_H

#include <stddef.h>
#include <stdint.h>
#include "finnproto.h"

/*
 * finnbrowser — the wire between Finn Browser's view and its page engine.
 *
 * Finn Browser is two pieces. The chrome — tabs, address bar, buttons,
 * find — is drawn by finn-comp with the shell's own primitives, like Finn
 * Files and Finn Store. Only the page rectangle comes from an engine:
 * Chromium, run headless and out of process by `finn-browser-engine`, which
 * finnd starts through `app.launch browser` like any other app. No toolkit
 * window ever exists.
 *
 * The engine speaks the DevTools protocol to Chromium over the pipe pair on
 * descriptors 3 and 4. JSON stops there: what crosses this socket is the
 * same kind of thing the control plane carries — short text lines — plus
 * one binary payload, a decoded frame.
 *
 * The socket
 *   FINN_BROWSER_SOCK, created by finn-comp: owned by root, group finn,
 *   mode 0660. The compositor checks SO_PEERCRED on every accept and takes
 *   root or the finn user only. It holds one engine at a time; a second
 *   connection is refused with `ERR busy`. It is a provider socket, like
 *   comp.sock: it carries pixels and input for one view, never authority.
 *
 * Lines
 *   One message per line, terminated by '\n', at most FINN_BROWSER_LINE_MAX
 *   bytes including the newline. Fields are separated by one space. A
 *   message with free text (a URL, a title, find text) carries it as its
 *   last field, running to the end of the line; it has been passed through
 *   finn_browser_text_clean(), so it holds no control characters.
 *
 * Engine -> compositor
 *   HELLO <version>                  first line; version is FINN_BROWSER_VERSION
 *   UNAVAILABLE <reason>             no engine on this machine; the helper exits
 *   READY                            Chromium is up and answering
 *   TAB <tab>                        a tab exists (created by either side)
 *   CLOSED <tab>                     a tab is gone
 *   ACTIVE <tab>                     this tab is the one that streams frames
 *   URL <tab> <url>                  the tab's committed address
 *   TITLE <tab> <title>              the page title
 *   LOADING <tab> <0|1>              the page is (or stopped) loading
 *   HISTORY <tab> <back 0|1> <forward 0|1>
 *   FOUND <tab> <matches> <found 0|1>
 *                                    reply to FIND: the page's own match count
 *                                    (-1 when the page would not say) and
 *                                    whether a match is now selected
 *   FRAME <tab> <w> <h>              followed by exactly w*h*4 bytes of
 *                                    native-endian 0xFFRRGGBB pixels, rows top
 *                                    to bottom, no padding
 *   ERROR <tab> <text>               a request failed; tab 0 means the engine
 *   BYE <reason>                     the engine is stopping (Chromium exited)
 *
 * Compositor -> engine
 *   VIEWPORT <w> <h> <scale_milli>   the page rectangle in scanout pixels and
 *                                    the shell's scale x1000; every tab is
 *                                    laid out at w/scale by h/scale CSS pixels
 *   NEW <url>                        open a tab; the url may be empty
 *   CLOSE <tab>
 *   ACTIVATE <tab>
 *   NAVIGATE <tab> <url>             url is already normalised (see below)
 *   BACK <tab> | FORWARD <tab> | RELOAD <tab> | STOP <tab>
 *   MOUSE <tab> <press|release|move> <x> <y> <none|left|middle|right> <clicks> <mods>
 *   WHEEL <tab> <x> <y> <dx> <dy>    x/y in scanout pixels relative to the
 *                                    page rectangle; dx/dy in CSS pixels
 *   KEY <tab> <down|up> <key> <code> <vk> <mods>
 *                                    key/code are DOM KeyboardEvent names
 *                                    ("Enter", "ArrowLeft", "KeyA"); vk is the
 *                                    Windows virtual key code
 *   TEXT <tab> <text>                committed text, inserted as typed
 *   FIND <tab> <forward|backward> <text>
 *   FINDEND <tab>
 *   QUIT
 *
 *   <mods> is the DevTools bit set: 1 Alt, 2 Ctrl, 4 Meta, 8 Shift.
 */

#define FINN_BROWSER_SOCK        "/run/finn/browser.sock"
#define FINN_BROWSER_VERSION     1

/* The engine's catalogue id and the flatpak that supplies Chromium. finnd
 * launches the first through the registry; the helper checks for the
 * second and says UNAVAILABLE when it is not installed. */
#define FINN_BROWSER_APP_ID      "browser"
#define FINN_BROWSER_ENGINE_REF  "org.chromium.Chromium"

#define FINN_BROWSER_LINE_MAX    4096
#define FINN_BROWSER_TAB_MAX     12
#define FINN_BROWSER_URL_MAX     2048
#define FINN_BROWSER_TITLE_MAX   256
#define FINN_BROWSER_FIND_MAX    256

/* A frame is bounded before a byte of it is allocated. The dimension cap is
 * the Wayland client one; the pixel cap is the window manager's per-client
 * budget, so a page can never claim more than a client window could. */
#define FINN_BROWSER_FRAME_MAX_DIM     8192
#define FINN_BROWSER_FRAME_MAX_PIXELS  (16u * 1024u * 1024u)

/* The scale the shell may announce, x1000. Outside it the VIEWPORT is a
 * malformed request, not a zoom level. */
#define FINN_BROWSER_SCALE_MIN_MILLI   500
#define FINN_BROWSER_SCALE_MAX_MILLI   4000

/*
 * Parses a `FRAME <tab> <w> <h>` line (without its newline) and checks the
 * bounds. Returns 0 and fills the outputs, or -1 for anything malformed,
 * out of range or over the pixel cap. The byte count that follows is
 * finn_browser_frame_bytes(w, h).
 */
int    finn_browser_frame_parse(const char *line, int *tab, int *w, int *h);
size_t finn_browser_frame_bytes(int w, int h);

/* Writes `FRAME <tab> <w> <h>\n` into buf. Returns its length, or -1 when
 * the frame is out of bounds or buf is too small. */
int    finn_browser_frame_header(char *buf, size_t cap, int tab, int w, int h);

/*
 * Makes a string safe to be the last field of a line: every byte below
 * 0x20 and 0x7f becomes a space, and the result is truncated at cap-1 bytes
 * on a UTF-8 boundary. dst and src may be the same buffer.
 */
void   finn_browser_text_clean(char *dst, size_t cap, const char *src);

/*
 * What the address bar does with what was typed. Returns:
 *   FINN_BROWSER_ADDR_URL      out holds an absolute http(s)/about/file URL
 *   FINN_BROWSER_ADDR_NOT_URL  the text is not an address (Finn Browser has
 *                              no search provider, and says so)
 *   FINN_BROWSER_ADDR_INVALID  empty, too long, or a scheme it will not load
 * "example.com" becomes "https://example.com/"; "localhost:8080/x" becomes
 * "http://localhost:8080/x"; "javascript:", "data:" and unknown schemes are
 * refused rather than handed to the engine.
 */
enum {
    FINN_BROWSER_ADDR_URL = 0,
    FINN_BROWSER_ADDR_NOT_URL = 1,
    FINN_BROWSER_ADDR_INVALID = 2
};
int    finn_browser_address(const char *typed, char *out, size_t cap);

#endif
