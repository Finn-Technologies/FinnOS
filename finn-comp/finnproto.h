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

#define FINN_MAX_LINE      4096

#endif
