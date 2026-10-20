/* finnaudio — the master output level, through ALSA. */
#define _GNU_SOURCE
#include "finnaudio.h"

#include <stdio.h>
#include <string.h>
#include <alloca.h>
#include <alsa/asoundlib.h>

/* Preferred simple-mixer elements, best first. A machine normally exposes
 * exactly one of these; "default" alone is not enough because the first
 * element on a card is often "Capture" or a bogus control. */
static const char *const CANDIDATES[] = {
    "Master", "PCM", "Speaker", "Headphone", "Front", "Digital", NULL
};

static char g_elem[64];

static int mixer_open(snd_mixer_t **mp, snd_mixer_elem_t **ep) {
    snd_mixer_t *m = NULL;
    if (snd_mixer_open(&m, 0) < 0) return -1;
    if (snd_mixer_attach(m, "default") < 0) goto fail;
    if (snd_mixer_selem_register(m, NULL, NULL) < 0) goto fail;
    if (snd_mixer_load(m) < 0) goto fail;

    snd_mixer_elem_t *e = NULL;
    for (int i = 0; CANDIDATES[i]; i++) {
        snd_mixer_selem_id_t *sid = NULL;
        snd_mixer_selem_id_alloca(&sid);
        snd_mixer_selem_id_set_index(sid, 0);
        snd_mixer_selem_id_set_name(sid, CANDIDATES[i]);
        snd_mixer_elem_t *cand = snd_mixer_find_selem(m, sid);
        if (cand && snd_mixer_selem_has_playback_volume(cand)) { e = cand; break; }
    }

    if (!e) {
        for (e = snd_mixer_first_elem(m); e; e = snd_mixer_elem_next(e))
            if (snd_mixer_selem_has_playback_volume(e)) break;
    }
    if (!e) goto fail;

    const char *nm = snd_mixer_selem_get_name(e);
    snprintf(g_elem, sizeof g_elem, "%s", nm ? nm : "?");
    *mp = m;
    *ep = e;
    return 0;

fail:
    if (m) snd_mixer_close(m);
    return -1;
}

int finn_audio_get(void) {
    snd_mixer_t *m;
    snd_mixer_elem_t *e;
    if (mixer_open(&m, &e) != 0) return -1;

    long min = 0, max = 0, v = 0;
    int rc = snd_mixer_selem_get_playback_volume_range(e, &min, &max);
    if (rc < 0 || max <= min) { snd_mixer_close(m); return -1; }
    if (snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &v) < 0) {
        snd_mixer_close(m);
        return -1;
    }
    snd_mixer_close(m);

    int pct = (int)((v - min) * 100 / (max - min));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

int finn_audio_set(int pct) {
    snd_mixer_t *m;
    snd_mixer_elem_t *e;
    if (mixer_open(&m, &e) != 0) return -1;

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    long min = 0, max = 0;
    if (snd_mixer_selem_get_playback_volume_range(e, &min, &max) < 0 || max <= min) {
        snd_mixer_close(m);
        return -1;
    }

    long v = min + (max - min) * (long)pct / 100;
    int rc = snd_mixer_selem_set_playback_volume_all(e, v);

    /* silence at 0, unmute above it — otherwise the level moves and nothing
     * is audible, which is the classic muted-mixer surprise */
    if (snd_mixer_selem_has_playback_switch(e))
        snd_mixer_selem_set_playback_switch_all(e, pct > 0);

    snd_mixer_close(m);
    return rc < 0 ? -1 : 0;
}

int finn_audio_present(void) {
    return finn_audio_get() >= 0;
}

const char *finn_audio_element(void) {
    if (finn_audio_get() < 0) return NULL;
    return g_elem;
}
