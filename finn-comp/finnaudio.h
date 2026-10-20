#ifndef FINNAUDIO_H
#define FINNAUDIO_H

/*
 * finnaudio — the master output level, through ALSA.
 *
 * Volume is not a number the compositor invents: it is a property of the
 * sound hardware. This is the one place that knows how to read and program
 * it, so the session audio daemon and the Control Center agree by
 * construction rather than by convention.
 *
 * Every entry point returns -1 when there is no usable playback mixer, which
 * is the honest answer on a machine with no audio hardware (a VM with a bare
 * HDA codec, for instance). Callers must treat that as "unavailable", never
 * as zero.
 */

/* Master playback level as 0..100, or -1 when there is no mixer. */
int finn_audio_get(void);

/* Programs the mixer. 0 on success, -1 when there is no mixer. */
int finn_audio_set(int pct);

/* 1 when a playback mixer exists. */
int finn_audio_present(void);

/* Name of the element that was found ("Master", "PCM", …), or NULL. */
const char *finn_audio_element(void);

#endif
