"""
`finn-sdk` — the control plane from a shell.

    finn-sdk state
    finn-sdk state --volume 40
    finn-sdk apps
    finn-sdk launch notes
    finn-sdk shot /tmp/desktop.ppm
    finn-sdk power restart

The common verbs, with shell-friendly names. The protocol itself has more:
packaging, Flatpak, Wine, winget, updates and the Files, Settings and setup
windows are reached through `finnctl`.
"""

import argparse
import os
import sys

from .client import FinnClient, FinnError
from .protocol import ProtocolError


def _client(args):
    return FinnClient(path=args.socket, timeout=args.timeout, direct=args.direct)


def cmd_state(args):
    with _client(args) as finn:
        if args.clear:
            finn.clear_state()
            print("state cleared")
            return 0
        if any(v is not None for v in (args.battery, args.volume, args.brightness,
                                       args.wifi, args.bluetooth, args.charging,
                                       args.dark)):
            values = {}
            if args.battery is not None:
                values["battery"] = args.battery
            if args.volume is not None:
                values["volume"] = args.volume
            if args.brightness is not None:
                values["brightness"] = args.brightness
            if args.wifi is not None:
                values["wifi"] = 1 if args.wifi else 0
            if args.bluetooth is not None:
                values["bluetooth"] = 1 if args.bluetooth else 0
            if args.charging is not None:
                values["charging"] = 1 if args.charging else 0
            if args.dark is not None:
                values["dark"] = 1 if args.dark else 0
            finn.set_state(**values)
            print("state set")
        state = finn.state()
        for key, value in sorted(state.as_dict().items()):
            print("%-12s %s" % (key, value))
    return 0


def cmd_screen(args):
    with _client(args) as finn:
        if args.shell:
            for key, value in sorted(finn.screen_shell().items()):
                print("%-16s %s" % (key, value))
        else:
            for key, value in sorted(finn.screen_info().items()):
                print("%-16s %s" % (key, value))
    return 0


def cmd_shot(args):
    with _client(args) as finn:
        data = finn.capture(step=args.step)
    if args.output == "-":
        sys.stdout.buffer.write(data)
    else:
        with open(args.output, "wb") as handle:
            handle.write(data)
        print("%s (%d bytes)" % (args.output, len(data)))
    return 0


def cmd_pointer(args):
    with _client(args) as finn:
        if args.x is not None and args.y is not None:
            finn.move(args.x, args.y)
        if args.dx or args.dy:
            finn.move_by(args.dx or 0, args.dy or 0)
        if args.click:
            finn.click(args.button)
        for key, value in sorted(finn.pointer().items()):
            print("%-8s %s" % (key, value))
    return 0


def cmd_apps(args):
    with _client(args) as finn:
        for app in finn.apps():
            print("%-10s %-10s %s" % (app.id, app.state, app.name))
    return 0


def cmd_launch(args):
    with _client(args) as finn:
        result = finn.launch(args.app_id)
        print(result or "launched")
    return 0


def cmd_notify(args):
    with _client(args) as finn:
        if args.title:
            finn.notify(args.title, args.body or "")
            print("posted")
            return 0
        if args.clear:
            finn.clear_notifications()
            print("cleared")
            return 0
        for note in finn.notifications():
            print("%s  %s — %s" % (note["at"], note["title"], note["body"]))
    return 0


def cmd_power(args):
    with _client(args) as finn:
        finn.power(args.what)
        print(args.what)
    return 0


def cmd_appearance(args):
    with _client(args) as finn:
        finn.set_appearance(args.dark)
        print("dark" if args.dark else "light")
    return 0


def cmd_ping(args):
    with _client(args) as finn:
        print(finn.ping())
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="finn-sdk", description=__doc__)
    parser.add_argument("--socket", help="the control socket to use")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--direct", action="store_true",
                        help="talk to the compositor's socket, not the control plane")
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("state", help="read or write the system state")
    p.add_argument("--battery", type=int)
    p.add_argument("--volume", type=int)
    p.add_argument("--brightness", type=int)
    p.add_argument("--wifi", type=lambda v: v.lower() in ("1", "on", "true"))
    p.add_argument("--bluetooth", type=lambda v: v.lower() in ("1", "on", "true"))
    p.add_argument("--charging", type=lambda v: v.lower() in ("1", "on", "true"))
    p.add_argument("--dark", type=lambda v: v.lower() in ("1", "on", "true"))
    p.add_argument("--clear", action="store_true")
    p.set_defaults(func=cmd_state)

    p = sub.add_parser("screen", help="the screen and the shell")
    p.add_argument("--shell", action="store_true")
    p.set_defaults(func=cmd_screen)

    p = sub.add_parser("shot", help="capture the frame")
    p.add_argument("output")
    p.add_argument("--step", type=int, default=1)
    p.set_defaults(func=cmd_shot)

    p = sub.add_parser("pointer", help="move and click")
    p.add_argument("--x", type=int)
    p.add_argument("--y", type=int)
    p.add_argument("--dx", type=int, default=0)
    p.add_argument("--dy", type=int, default=0)
    p.add_argument("--click", action="store_true")
    p.add_argument("--button", default="left")
    p.set_defaults(func=cmd_pointer)

    p = sub.add_parser("apps", help="the application registry")
    p.set_defaults(func=cmd_apps)

    p = sub.add_parser("launch", help="start an app")
    p.add_argument("app_id")
    p.set_defaults(func=cmd_launch)

    p = sub.add_parser("notify", help="post or list notifications")
    p.add_argument("title", nargs="?")
    p.add_argument("body", nargs="?")
    p.add_argument("--clear", action="store_true")
    p.set_defaults(func=cmd_notify)

    p = sub.add_parser("power", help="shut down or restart the machine")
    p.add_argument("what", nargs="?", default="restart")
    p.set_defaults(func=cmd_power)

    p = sub.add_parser("appearance", help="light or dark")
    p.add_argument("dark", type=lambda v: v.lower() in ("1", "dark", "true"))
    p.set_defaults(func=cmd_appearance)

    p = sub.add_parser("ping", help="is the control plane alive")
    p.set_defaults(func=cmd_ping)

    args = parser.parse_args(argv)
    if not getattr(args, "func", None):
        parser.print_help()
        return 2

    try:
        return args.func(args)
    except FinnError as exc:
        print("finn-sdk: %s" % exc, file=sys.stderr)
        return 1
    except (ProtocolError, OSError) as exc:
        print("finn-sdk: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
