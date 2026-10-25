#ifndef FINNAPPS_H
#define FINNAPPS_H

/*
 * finnapps — the FinnOS application registry.
 *
 * Two files, two owners:
 *
 *   /usr/share/finn/apps.registry   the catalogue. Shipped with the OS and
 *                                   owned by it: it says which apps exist, what
 *                                   they are called and what they look like.
 *   /var/lib/finnpkg/installed      what the package manager has installed.
 *                                   Owned by finnpkg, and the only thing that
 *                                   says whether an app is on this machine.
 *
 * Keeping them apart is what lets the shell show an app that is honestly
 * "not installed" without the package manager having to edit a file the OS
 * ships, and it keeps a catalogue update from wiping the user's install
 * list.
 *
 * The catalogue format is one app per line, pipe separated:
 *
 *   id | Name | icon.svg | /usr/bin/thing | note
 *
 * Blank lines and lines starting with '#' are ignored. The first field is the
 * id the rest of the system uses; the rest are for humans and for the shell.
 */

#define FINN_APPS_CATALOGUE "/usr/share/finn/apps.registry"
#define FINN_APPS_INSTALLED "/var/lib/finnpkg/installed"

#define FINN_APP_MAX 64

typedef struct {
    char id[32];
    char name[48];
    char icon[32];
    char command[128];
    char note[64];
    char source[16];
    int  installed;      /* 1 when the package manager has it on this machine */
} finn_app;

/*
 * Read the catalogue and the install list. Returns the number of apps in the
 * catalogue, or -1 when the catalogue could not be read at all — which is a
 * different thing from an empty catalogue, and the shell says so.
 *
 * The install list is optional: a machine with no packages on it has no
 * install list, and that is not an error.
 */
int  finn_apps_load(const char *catalogue, const char *installed);

void finn_apps_free(void);

int  finn_apps_count(void);
const finn_app *finn_apps_at(int i);
const finn_app *finn_apps_find(const char *id);

/* How many of the catalogue's apps are installed. */
int  finn_apps_installed_count(void);

#endif
