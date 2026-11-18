#!/usr/bin/env python3
"""Generate the C++ design tokens from ui/_tokens.fui.

The design file is the source. The generated header is committed so the tree
builds without Python, and tests/unit/test_tokens.py fails when the two
disagree.
"""

import re
import sys
from pathlib import Path

COLOR = re.compile(
    r"^\s*color:\s*(\w+)_light\s+#([0-9A-Fa-f]{6,8})\s*,"
    r"\s*\1_dark\s+#([0-9A-Fa-f]{6,8})",
    re.MULTILINE,
)
LINE_PAIR = re.compile(r"^\s*(spacing|radius|blur|elevation|breakpoint):\s*(.+)$", re.MULTILINE)
ITEM = re.compile(r"\b([a-zA-Z_]\w*)\s+([0-9]+(?:\.[0-9]+)?)\b")
SMOOTHING = re.compile(r"\bcorner_smoothing\s+([0-9]+(?:\.[0-9]+)?)\b")


def strip_comment(line: str) -> str:
    idx = line.find("//")
    return line if idx < 0 else line[:idx]


def argb(value: str) -> str:
    if len(value) == 6:
        value = "FF" + value
    return "0x" + value.upper() + "u"


def parse_pairs(source: str, kind: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for raw_line in source.splitlines():
        cleaned = strip_comment(raw_line)
        m = LINE_PAIR.match(cleaned)
        if not m or m.group(1) != kind:
            continue
        out.extend(ITEM.findall(m.group(2)))
    return out


def generate(source: str) -> str:
    tokens = COLOR.findall(source)
    if not tokens:
        raise SystemExit("no colour tokens found")

    spacings = parse_pairs(source, "spacing")
    radii = parse_pairs(source, "radius")
    blurs = parse_pairs(source, "blur")
    elevations = parse_pairs(source, "elevation")
    breakpoints = parse_pairs(source, "breakpoint")
    smoothing_match = SMOOTHING.search(
        "\n".join(strip_comment(l) for l in source.splitlines())
    )

    lines = [
        "// Generated from ui/_tokens.fui by tools/tokens.py. Do not edit.",
        "#ifndef FINN_UI_GENERATED_TOKENS_HPP",
        "#define FINN_UI_GENERATED_TOKENS_HPP",
        "",
        "#include <cstdint>",
        "",
        "namespace finn::ui::colors {",
        "",
    ]
    for name, light, dark in tokens:
        lines.append(f"inline constexpr std::uint32_t {name}_light = {argb(light)};")
        lines.append(f"inline constexpr std::uint32_t {name}_dark = {argb(dark)};")
    lines += ["", "}  // namespace finn::ui::colors"]

    if spacings:
        lines += ["", "namespace finn::ui::spacing {", ""]
        for name, val in spacings:
            lines.append(f"inline constexpr int {name} = {int(float(val))};")
        lines += ["", "}  // namespace finn::ui::spacing"]

    if radii or smoothing_match:
        lines += ["", "namespace finn::ui::radii {", ""]
        for name, val in radii:
            fval = float(val)
            lines.append(f"inline constexpr double {name} = {fval};")
        if smoothing_match:
            lines.append(
                f"inline constexpr double corner_smoothing = {float(smoothing_match.group(1))};"
            )
        lines += ["", "}  // namespace finn::ui::radii"]

    if blurs:
        lines += ["", "namespace finn::ui::blur {", ""]
        for name, val in blurs:
            lines.append(f"inline constexpr int {name} = {int(float(val))};")
        lines += ["", "}  // namespace finn::ui::blur"]

    if elevations:
        lines += ["", "namespace finn::ui::elevation {", ""]
        for name, val in elevations:
            lines.append(f"inline constexpr int {name} = {int(float(val))};")
        lines += ["", "}  // namespace finn::ui::elevation"]

    if breakpoints:
        lines += ["", "namespace finn::ui::breakpoints {", ""]
        for name, val in breakpoints:
            lines.append(f"inline constexpr int {name} = {int(float(val))};")
        lines += ["", "}  // namespace finn::ui::breakpoints"]

    lines += ["", "#endif", ""]
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tokens.py ui/_tokens.fui src/ui/generated_tokens.hpp", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text()
    Path(sys.argv[2]).write_text(generate(source))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
