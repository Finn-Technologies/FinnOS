"""
`fuic` — the FinnUI compiler.

    fuic compile in.fui -o out.fbin
    fuic check   in.fui
    fuic dump    in.fui
    fuic new     <app>
"""

import argparse
import os
import sys

from .compiler import CompileError, compile_file, compile_source, dump
from .lexer import LexError
from .parser import ParseError, parse
from .theme import Theme

# The app templates. They sit beside the package in the source tree, so a
# checkout and an editable install both find them. A wheel does not carry
# them, and `fuic new` says so rather than writing a broken app.
TEMPLATE_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "templates")


def cmd_compile(args):
    warnings, _ = compile_file(args.source, args.output, args.theme, args.strict)
    for warning in warnings:
        print("fuic: warning: %s" % warning, file=sys.stderr)
    if args.output:
        print("fuic: wrote %s" % args.output)
    return 0


def cmd_check(args):
    try:
        with open(args.source, "r", encoding="utf-8") as handle:
            text = handle.read()
        items = parse(text)
    except (CompileError, ParseError, LexError) as exc:
        print("fuic: %s" % exc, file=sys.stderr)
        return 1

    theme = Theme.from_items(items)
    screens = [i for i in items if i.kind == "screen"]
    targets = [i for i in items if i.kind == "target"]
    themes = [i for i in items if i.kind == "theme"]

    print("%s: %d screen(s), %d target(s), %d theme(s), %d token(s)"
          % (args.source, len(screens), len(targets), len(themes),
             len(theme.tokens)))
    for warning in theme.warnings:
        print("fuic: warning: %s" % warning, file=sys.stderr)
    return 0


def cmd_dump(args):
    try:
        with open(args.source, "r", encoding="utf-8") as handle:
            items = parse(handle.read())
    except (ParseError, LexError) as exc:
        print("fuic: %s" % exc, file=sys.stderr)
        return 1

    for item in items:
        print("\n".join(dump(item)))
        print()
    return 0


def cmd_new(args):
    """Make a new app from the template."""
    name = args.name
    if not name:
        print("fuic: new needs a name", file=sys.stderr)
        return 2

    templates = template_dir()
    if templates is None:
        print("fuic: the app templates are not here (looked in %s)"
              % TEMPLATE_DIR, file=sys.stderr)
        print("fuic: install the SDK from a checkout: pip install -e sdk",
              file=sys.stderr)
        return 1

    # The house rule: an app is the brand and a plain English noun.
    title = name if name.startswith("Finn ") else "Finn " + " ".join(
        part.capitalize() for part in name.replace("-", " ").split())
    slug = name.lower().replace(" ", "-").replace("finn-", "")
    noun = title.split()[-1]

    target_dir = args.directory or os.path.join("apps", slug)
    os.makedirs(target_dir, exist_ok=True)

    written = []
    for template, filename in (("app.fui", noun + ".fui"),
                                ("manifest.toml", "manifest.toml")):
        path = os.path.join(target_dir, filename)
        with open(os.path.join(templates, template), "r", encoding="utf-8") as handle:
            text = handle.read()
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text % {"title": title, "name": noun, "id": slug})
        written.append(path)

    for path in written:
        print("fuic: wrote %s" % path)
    print("fuic: add it to assets/apps.registry when it has a command")
    return 0


def template_dir():
    """Where the app templates are, or None when they are not here."""
    if not os.path.isdir(TEMPLATE_DIR):
        return None
    return TEMPLATE_DIR


def main(argv=None):
    parser = argparse.ArgumentParser(prog="fuic", description=__doc__)
    sub = parser.add_subparsers(dest="command")

    p_compile = sub.add_parser("compile", help="compile a .fui file")
    p_compile.add_argument("source")
    p_compile.add_argument("-o", "--output")
    p_compile.add_argument("--theme", help="a theme file to resolve against")
    p_compile.add_argument("--strict", action="store_true",
                           help="treat warnings as errors")
    p_compile.set_defaults(func=cmd_compile)

    p_check = sub.add_parser("check", help="parse a .fui file and report")
    p_check.add_argument("source")
    p_check.set_defaults(func=cmd_check)

    p_dump = sub.add_parser("dump", help="print the tree")
    p_dump.add_argument("source")
    p_dump.set_defaults(func=cmd_dump)

    p_new = sub.add_parser("new", help="make an app from the template")
    p_new.add_argument("name")
    p_new.add_argument("-d", "--directory")
    p_new.set_defaults(func=cmd_new)

    args = parser.parse_args(argv)
    if not getattr(args, "func", None):
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
