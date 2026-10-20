#ifndef FINNANIM_H
#define FINNANIM_H

/*
 * finnanim — demand-driven animation for the FinnOS shell.
 *
 * The compositor is event-driven and idles at zero frames per second, so
 * animation cannot be a free-running timer. A track that is running has to
 * ask for the next frame, and the moment the last track settles the shell
 * goes back to blocking on input. Everything here exists to make that
 * question answerable — "does this need a frame?" — and to shape the number
 * the shell then draws with.
 *
 * A track carries no notion of wall time of its own. The caller advances it
 * with the seconds elapsed since the previous frame, which keeps motion
 * locked to the display's page flips instead of to a timer that drifts away
 * from them.
 */

#include <stdint.h>

typedef enum {
    FINN_EASE_LINEAR,
    FINN_EASE_OUT,        /* quick off the mark, settles softly */
    FINN_EASE_IN,         /* winds up, arrives at speed         */
    FINN_EASE_IN_OUT,
    FINN_EASE_OUT_BACK    /* a little overshoot: the springy one */
} finn_ease;

typedef struct {
    float     from, to;   /* endpoints                     */
    float     cur;        /* the value the shell draws     */
    float     dur;        /* seconds                       */
    float     t;          /* elapsed seconds               */
    float     delay;      /* seconds held before it starts */
    finn_ease ease;
    int       active;
} finn_anim;

/* The shaped progress, 0..1, with FINN_EASE_OUT_BACK free to exceed 1. */
float finn_ease_at(finn_ease e, float t);

/* Land on a value with no motion and no frame requested. */
void  finn_anim_jump(finn_anim *a, float v);

/* Head for a target. Re-aiming at the target a track already has, or asking
 * a settled track to stay where it is, does nothing — which is what lets the
 * callers shout their desired value every frame without restarting motion. */
void  finn_anim_to(finn_anim *a, float target, float dur, float delay, finn_ease e);

/* Advance by dt seconds. Returns 1 whenever the value moved this frame,
 * including the frame that settles it, so a caller knows to redraw. Returns
 * 0 once the track is parked. */
int   finn_anim_tick(finn_anim *a, float dt);

int   finn_anim_running(const finn_anim *a);
float finn_anim_v(const finn_anim *a);

#endif
