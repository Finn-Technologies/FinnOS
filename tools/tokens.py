#!/usr/bin/env python3
"""Generate the C++ colour tokens from ui/_tokens.fui.

The design file is the source. The generated header is committed so the tree
builds without Python, and tests/rewrite/test_tokens.py fails when the two
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


def argb(value: str) -> str:
    if len(value) == 6:
        value = "FF" + value
    return "0x" + value.upper() + "u"


def generate(source: str) -> str:
    tokens = COLOR.findall(source)
    if not tokens:
        raise SystemExit("no colour tokens found")
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
    lines += ["", "}  // namespace finn::ui::colors", "", "#endif", ""]
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
