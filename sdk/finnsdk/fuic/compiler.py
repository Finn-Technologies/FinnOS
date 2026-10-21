"""
The .fui compiler.

    fuic compile ui/StartMenu.fui -o StartMenu.fbin
    fuic check   ui/StartMenu.fui
    fuic dump    ui/StartMenu.fui
    fuic new     my-app

A compile is: parse, resolve the theme's tokens into the tree, and write
the binary. The tokens are resolved at compile time on purpose — a
renderer should not have to carry the theme to draw one screen, and the
theme is what the appearance switch changes, so a themed colour stays
themed in the file and is resolved at the last moment.
"""

import os
import sys

from . import fbin
from .parser import ParseError, parse
from .lexer import LexError
from .theme import Theme


class CompileError(Exception):
    pass


def compile_source(text, theme=None, strict=False):
    """
    Compile .fui source into (sections, warnings).

    `sections` is a list of (name, payload) pairs, ready for fbin.write().
    """
    try:
        items = parse(text)
    except (ParseError, LexError) as exc:
        raise CompileError(str(exc)) from exc

    if theme is None:
        theme = Theme.from_items(items)

    screens = [item for item in items if item.kind == "screen"]
    targets = [item for item in items if item.kind == "target"]
    themes = [item for item in items if item.kind == "theme"]
    if not screens and not targets and not themes:
        # A file that declares nothing is a mistake; a file that declares
        # only a theme or only targets is a library, and compiles to a
        # binary with those sections and nothing else.
        raise CompileError("nothing to compile: no screen, theme or target")

    sections = []

    for target in targets:
        payload = fbin.encode_node(target)
        sections.append(("target:" + target.name, payload))

    for index, screen in enumerate(screens):
        theme.resolve_node(screen)
        payload = fbin.compile_screen(screen)
        # A file holds one screen, and its section is `screen`. A file
        # that declares more keeps them all, each under its own name,
        # rather than dropping all but the last.
        name = "screen" if index == 0 else "screen:" + screen.name
        sections.append((name, payload))

    sections.append(("tokens", fbin.encode_value(
        {"k": "map", "v": [(k, v) for k, v in sorted(theme.tokens.items())]})))

    warnings = list(theme.warnings)
    if strict and warnings:
        raise CompileError("; ".join(warnings))
    return sections, warnings


def find_theme(path):
    """
    The theme a screen is compiled against.

    The theme beside the screen comes first, because an app may have one
    of its own. Then the theme of the system, which lives in
    `ui/_tokens.fui`: the apps in `apps/` are part of the same system and
    use the same tokens as the shell.
    """
    directory = os.path.dirname(os.path.abspath(path))
    for _ in range(4):
        beside = os.path.join(directory, "_tokens.fui")
        if os.path.exists(beside):
            return beside
        system = os.path.join(directory, "ui", "_tokens.fui")
        if os.path.exists(system):
            return system
        parent = os.path.dirname(directory)
        if parent == directory:
            break
        directory = parent
    return None


def compile_file(path, out_path=None, theme_path=None, strict=False):
    """Compile a .fui file. Returns the warnings."""
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()

    theme = None
    if theme_path:
        with open(theme_path, "r", encoding="utf-8") as handle:
            theme = Theme.from_items(parse(handle.read()))
    else:
        beside = find_theme(path)
        if beside:
            with open(beside, "r", encoding="utf-8") as handle:
                theme = Theme.from_items(parse(handle.read()))

    sections, warnings = compile_source(text, theme=theme, strict=strict)

    data = fbin.write(sections)
    if out_path:
        with open(out_path, "wb") as handle:
            handle.write(data)
    return warnings, data


def dump(node, indent=0):
    """A readable form of a tree, for `fuic dump`.

    It takes either a parsed tree or one that has been read back out of
    a `.fbin`, because both are worth looking at.
    """
    if isinstance(node, dict):
        name = node.get("name", "?")
        kind = node.get("kind", "")
        props = node.get("props", {})
        args = node.get("args", [])
        children = node.get("children", [])
    else:
        name = node.name
        kind = node.kind
        props = node.props
        args = node.args
        children = node.children

    lines = []
    pad = "  " * indent
    shown = ["%s=%s" % (key, _show(value)) for key, value in props.items()]
    shown += [_show(a) for a in args]
    head = "%s %s" % (name, kind)
    if shown:
        head += "  " + " ".join(shown)
    lines.append(pad + head)
    for child in children:
        lines.extend(dump(child, indent + 1))
    return lines


def _show(value):
    kind = value.get("k")
    if kind == "str":
        return '"%s"' % value["v"]
    if kind in ("int", "num", "bool"):
        return str(value["v"])
    if kind == "ident":
        return value["v"]
    if kind == "colour":
        return value["v"]
    if kind == "list":
        return "[" + ", ".join(_show(v) for v in value["v"]) + "]"
    if kind == "map":
        return "{" + ", ".join("%s: %s" % (k, _show(v)) for k, v in value["v"]) + "}"
    if kind == "arrow":
        return "%s -> %s" % (_show(value["v"][0]), _show(value["v"][1]))
    if kind == "phrase":
        return " ".join(_show(v) for v in value["v"])
    if kind == "themed":
        return "%s/%s" % (value["light"].get("v"), value["dark"].get("v"))
    if kind == "combine":
        return "%s + %s" % (_show(value["v"][0]), _show(value["v"][1]))
    if kind == "call":
        name, args = value["v"]
        return "%s(%s)" % (_show(name), _show(args))
    return str(value.get("v"))
