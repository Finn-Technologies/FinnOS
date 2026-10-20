#ifndef FINNSTATE_H
#define FINNSTATE_H

/*
 * finnstate — the system state the FinnOS shell reflects.
 *
 * Sources, in priority order:
 *   1. /run/finn/state  — explicit overrides written by finnd or by the VM
 *      harness. This is a real interface: it lets a session daemon publish
 *      state, and it lets a test drive the shell through every state
 *      deterministically.
 *   2. The actual hardware — sysfs, procfs and the ALSA mixer.
 *
 * Every field has an "absent" value rather than a default, because the shell
 * has to be able to say "this machine does not have one" instead of showing
 * a plausible-looking lie. That is what makes the tray and the Control
 * Center adaptive without being mock.
 */

typedef struct {
    int battery_pct;   /* 0..100, or -1 when there is no battery (mains) */
    int charging;      /* 1 while charging or full                      */
    int wifi;          /* lit arcs 0..3, or -1 when there is no adapter */
    int volume;        /* 0..100, or -1 when unknown                    */
    int brightness;    /* 0..100, or -1 when there is no backlight      */
    int bluetooth;     /* 0 off, 1 on, or -1 when there is no adapter   */
    int wired;         /* 0 down, 1 up, or -1 when there is no wired link */
    int dark;          /* 1 = dark appearance, 0 = light                */
} finn_state;

void finn_state_read(finn_state *st);
int  finn_state_differs(const finn_state *a, const finn_state *b);

/* Publish an explicit override, for finnd and for tests. */
int  finn_state_set(const char *name, int value);
void finn_state_clear(void);

/*
 * Actuators. These are the real operations the Control Center performs, and
 * each returns 0 when it took effect and -1 when the hardware is not there —
 * so a control can only light up when it can actually do something.
 */
int finn_state_set_volume(int pct);
int finn_state_set_brightness(int pct);
int finn_state_set_bluetooth(int on);
int finn_state_set_wifi(int on);
int finn_state_set_dark(int on);

#endif
