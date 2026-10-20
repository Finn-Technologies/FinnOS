/* finnanim — demand-driven animation for the FinnOS shell. */
#include "finnanim.h"

#include <math.h>

/*
 * The curves are the shapes the shell actually uses. Everything the user
 * touches is either settling (OUT), being dismissed (IN_OUT) or popping
 * (OUT_BACK); there is deliberately no bounce-and-wobble, because a panel
 * that overshoots twice reads as a demo rather than as a system.
 */

static float ease_out(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static float ease_in(float t) {
    return t * t * t;
}

static float ease_in_out(float t) {
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) * 0.5f;
}

/* eases past the target and comes back, peaking around 1.10 */
static float ease_out_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

float finn_ease_at(finn_ease e, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    switch (e) {
    case FINN_EASE_OUT:      return ease_out(t);
    case FINN_EASE_IN:       return ease_in(t);
    case FINN_EASE_IN_OUT:   return ease_in_out(t);
    case FINN_EASE_OUT_BACK: return ease_out_back(t);
    default:                 return t;
    }
}

void finn_anim_jump(finn_anim *a, float v) {
    a->from = a->to = a->cur = v;
    a->t = a->dur = a->delay = 0.0f;
    a->active = 0;
}

void finn_anim_to(finn_anim *a, float target, float dur, float delay, finn_ease e) {
    if (a->active) {
        if (target == a->to) return;      /* already on its way there */
    } else if (target == a->cur) {
        return;                           /* already parked there     */
    }
    a->from   = a->cur;
    a->to     = target;
    a->dur    = dur;
    a->t      = 0.0f;
    a->delay  = delay;
    a->ease   = e;
    a->active = 1;
}

int finn_anim_tick(finn_anim *a, float dt) {
    if (!a->active) return 0;

    if (a->delay > 0.0f) {
        a->delay -= dt;
        if (a->delay > 0.0f) return 1;    /* still waiting, still animating */
        dt = -a->delay;                   /* carry what is left of the step */
        a->delay = 0.0f;
    }

    a->t += dt;
    float p = (a->dur > 0.0f) ? (a->t / a->dur) : 1.0f;

    if (p >= 1.0f) {
        a->cur = a->to;
        a->t = a->dur;
        a->active = 0;
        return 1;                         /* draw the resting value once */
    }

    a->cur = a->from + (a->to - a->from) * finn_ease_at(a->ease, p);
    return 1;
}

int   finn_anim_running(const finn_anim *a) { return a->active; }
float finn_anim_v(const finn_anim *a)       { return a->cur; }
