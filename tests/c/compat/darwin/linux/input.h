/*
 * A compile-only stand-in for Linux's <linux/input.h>, for the host build of
 * the Settings page test on macOS.
 *
 * The test links finnsettingsapp.c for its page metadata and its routing; it
 * never opens an input device and never delivers a key. Only the evdev codes
 * the file compares against have to exist, so this header carries the real
 * Linux values and nothing else.
 *
 * This directory is on the include path only when uname is Darwin (see
 * tests/c/Makefile). The device build uses the kernel's own header.
 */
#ifndef FINN_TEST_COMPAT_LINUX_INPUT_H
#define FINN_TEST_COMPAT_LINUX_INPUT_H

#define KEY_ESC        1
#define KEY_BACKSPACE  14
#define KEY_ENTER      28
#define KEY_A          30
#define KEY_KPENTER    96

#endif
