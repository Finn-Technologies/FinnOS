#!/usr/bin/env python3
"""A tiny absolute/relative uinput pointer for the FinnOS dev VM.

libinput only routes pointer events from a device that udev tags ID_INPUT_MOUSE.
ydotoold's device is tagged ID_INPUT_KEYBOARD, so its motion/button events are
dropped by the compositor.  This tool creates a device that advertises BTN_LEFT
plus ABS_X/ABS_Y, which udev tags as a mouse, and drives it over a unix socket.

Usage (as root on the VM):

    sudo python3 uinput-pointer.py serve --width 1440 --height 900 &

    python3 uinput-pointer.py send 'abs 400 300'
    python3 uinput-pointer.py send 'down left'
    python3 uinput-pointer.py send 'up left'
    python3 uinput-pointer.py send 'click 400 300 left'
    python3 uinput-pointer.py send 'quit'

Commands accepted by the server (one per connection):
    abs X Y          warp to absolute layout coordinates (0..width, 0..height)
    rel DX DY        relative motion
    down [left|right|middle]
    up   [left|right|middle]
    click X Y [btn]  warp, press, short pause, release
    tap X Y [btn]    alias for click
    quit             destroy the device and stop the server
"""

import argparse
import ctypes
import os
import socket
import struct
import time

_libc = ctypes.CDLL("libc.so.6", use_errno=True)


def ioctl(fd, request, arg):
    """Call libc ioctl directly: fcntl.ioctl rejects buffers over 1024 bytes."""
    if isinstance(arg, (bytes, bytearray)):
        buf = ctypes.create_string_buffer(bytes(arg))
        result = _libc.ioctl(fd, request, ctypes.byref(buf))
    else:
        result = _libc.ioctl(fd, request, ctypes.c_ulong(arg))
    if result < 0:
        errno = ctypes.get_errno()
        raise OSError(errno, "ioctl 0x%x failed" % request)
    return result

SOCKET_PATH = "/tmp/finn-uinput-pointer.sock"
DEVICE_NAME = "finn-uinput-pointer"
VENDOR, PRODUCT, VERSION = 0x1AF4, 0x0001, 0x0001  # virtio-ish, harmless

# --- linux/input-event-codes.h ------------------------------------------------
EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3
SYN_REPORT = 0
REL_X, REL_Y, REL_WHEEL = 0, 1, 8
ABS_X, ABS_Y = 0, 1
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE = 0x110, 0x111, 0x112
BTN_MAP = {"left": BTN_LEFT, "right": BTN_RIGHT, "middle": BTN_MIDDLE,
           "1": BTN_LEFT, "3": BTN_RIGHT, "2": BTN_MIDDLE}


def _ioc(dir_, type_, nr, size):
    return (dir_ << 30) | (size << 16) | (type_ << 8) | nr


_IOC_WRITE = 1
UINPUT_IOCTL_BASE = ord("U")

# sizeof(struct uinput_setup) = input_id(8) + name[80] + ff_effects_max(4)
UINPUT_SETUP_SIZE = 8 + 80 + 4
# sizeof(struct uinput_abs_setup) = __u16 code + 2 pad + input_absinfo(24)
UINPUT_ABS_SETUP_SIZE = 28

UI_DEV_CREATE = _ioc(0, UINPUT_IOCTL_BASE, 1, 0)
UI_DEV_DESTROY = _ioc(0, UINPUT_IOCTL_BASE, 2, 0)
UI_DEV_SETUP = _ioc(_IOC_WRITE, UINPUT_IOCTL_BASE, 3, UINPUT_SETUP_SIZE)
UI_ABS_SETUP = _ioc(_IOC_WRITE, UINPUT_IOCTL_BASE, 4, UINPUT_ABS_SETUP_SIZE)
UI_SET_EVBIT = _ioc(_IOC_WRITE, UINPUT_IOCTL_BASE, 100, 4)
UI_SET_KEYBIT = _ioc(_IOC_WRITE, UINPUT_IOCTL_BASE, 101, 4)
UI_SET_RELBIT = _ioc(_IOC_WRITE, UINPUT_IOCTL_BASE, 102, 4)
UI_SET_ABSBIT = _ioc(_IOC_WRITE, UINPUT_IOCTL_BASE, 103, 4)


def input_event(value, event_type, code, val):
    # struct input_event { struct timeval time; __u16 type; __u16 code; __s32 value; }
    return struct.pack("@llHHI", 0, 0, event_type, code, val)


class Pointer:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.fd = os.open("/dev/uinput", os.O_WRONLY)
        for ev in (EV_SYN, EV_KEY, EV_REL, EV_ABS):
            ioctl(self.fd, UI_SET_EVBIT, ev)
        for key in (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE):
            ioctl(self.fd, UI_SET_KEYBIT, key)
        for rel in (REL_X, REL_Y, REL_WHEEL):
            ioctl(self.fd, UI_SET_RELBIT, rel)
        for abs_ in (ABS_X, ABS_Y):
            ioctl(self.fd, UI_SET_ABSBIT, abs_)

        setup = struct.pack(
            "@HHHH80sI",
            VENDOR & 0xFFFF, PRODUCT & 0xFFFF, VERSION & 0xFFFF, 0,
            DEVICE_NAME.encode(), 0,
        )
        assert len(setup) == UINPUT_SETUP_SIZE, (len(setup), UINPUT_SETUP_SIZE)
        ioctl(self.fd, UI_DEV_SETUP, setup)
        # struct uinput_abs_setup { __u16 code; __u16 pad; struct input_absinfo; }
        for code, maximum in ((ABS_X, width), (ABS_Y, height)):
            abs_setup = struct.pack("@HH6i", code, 0, 0, 0, maximum, 0, 0, 1)
            assert len(abs_setup) == UINPUT_ABS_SETUP_SIZE
            ioctl(self.fd, UI_ABS_SETUP, abs_setup)
        ioctl(self.fd, UI_DEV_CREATE, 0)
        self.x = 0
        self.y = 0

    def emit(self, events):
        buf = b"".join(events) + input_event(0, EV_SYN, SYN_REPORT, 0)
        os.write(self.fd, buf)

    def abs_move(self, x, y):
        x = max(0, min(self.width, int(x)))
        y = max(0, min(self.height, int(y)))
        self.emit([input_event(0, EV_ABS, ABS_X, x),
                   input_event(0, EV_ABS, ABS_Y, y)])
        self.x, self.y = x, y

    def rel_move(self, dx, dy):
        self.emit([input_event(0, EV_REL, REL_X, int(dx)),
                   input_event(0, EV_REL, REL_Y, int(dy))])
        self.x += int(dx)
        self.y += int(dy)

    def button(self, which, down):
        code = BTN_MAP.get(which, BTN_LEFT)
        self.emit([input_event(0, EV_KEY, code, 1 if down else 0)])

    def click(self, x, y, which="left", hold=0.06):
        self.abs_move(x, y)
        time.sleep(0.03)
        self.button(which, True)
        time.sleep(hold)
        self.button(which, False)

    def destroy(self):
        try:
            ioctl(self.fd, UI_DEV_DESTROY, 0)
        finally:
            os.close(self.fd)


def serve(args):
    pointer = Pointer(args.width, args.height)
    if os.path.exists(SOCKET_PATH):
        os.unlink(SOCKET_PATH)
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(SOCKET_PATH)
    os.chmod(SOCKET_PATH, 0o666)
    listener.listen(8)
    print("ready: %s %dx%d" % (SOCKET_PATH, args.width, args.height), flush=True)
    running = True
    while running:
        conn, _ = listener.accept()
        with conn:
            data = b""
            while True:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                data += chunk
            line = data.decode(errors="replace").strip()
            if not line:
                continue
            parts = line.split()
            cmd = parts[0].lower()
            try:
                if cmd == "abs" and len(parts) >= 3:
                    pointer.abs_move(int(parts[1]), int(parts[2]))
                    result = "ok %d %d" % (pointer.x, pointer.y)
                elif cmd == "rel" and len(parts) >= 3:
                    pointer.rel_move(int(parts[1]), int(parts[2]))
                    result = "ok %d %d" % (pointer.x, pointer.y)
                elif cmd == "down":
                    pointer.button(parts[1] if len(parts) > 1 else "left", True)
                    result = "ok"
                elif cmd == "up":
                    pointer.button(parts[1] if len(parts) > 1 else "left", False)
                    result = "ok"
                elif cmd in ("click", "tap") and len(parts) >= 3:
                    which = parts[3] if len(parts) > 3 else "left"
                    pointer.click(int(parts[1]), int(parts[2]), which)
                    result = "ok"
                elif cmd == "quit":
                    result = "ok"
                    running = False
                else:
                    result = "unknown command: " + line
            except Exception as exc:  # noqa: BLE001 - report to the caller
                result = "error: %s" % exc
            conn.sendall(result.encode())
            print("%s -> %s" % (line, result), flush=True)
    listener.close()
    pointer.destroy()
    if os.path.exists(SOCKET_PATH):
        os.unlink(SOCKET_PATH)


def send(args):
    payload = args.command.encode()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(SOCKET_PATH)
    s.sendall(payload)
    s.shutdown(socket.SHUT_WR)
    reply = b""
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        reply += chunk
    s.close()
    if reply:
        print(reply.decode(errors="replace"))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=("serve", "send"))
    ap.add_argument("--width", type=int, default=1440)
    ap.add_argument("--height", type=int, default=900)
    ap.add_argument("command", nargs="?", default="")
    args = ap.parse_args()
    if args.mode == "serve":
        serve(args)
    else:
        if not args.command:
            ap.error("send needs a command string")
        send(args)


if __name__ == "__main__":
    main()
