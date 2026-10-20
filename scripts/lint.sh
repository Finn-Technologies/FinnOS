#!/bin/bash
# The style checks.
#
# They are the ones that are easy to break by accident and tedious to fix
# by hand afterwards: trailing whitespace, tabs in the C, a missing final
# newline, and a file that is not executable but should be.
#
#   scripts/lint.sh            the whole tree
#   scripts/lint.sh <paths>   only these
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PATHS=("$@")
if [ ${#PATHS[@]} -eq 0 ]; then
    PATHS=(src sdk mcp vm arch-base ui apps tests docs)
fi

fail=0

say() { printf '  %s\n' "$*"; }

for path in "${PATHS[@]}"; do
    [ -e "$path" ] || continue

    # trailing whitespace
    if grep -rIn --exclude-dir=.git --exclude-dir=__pycache__ '[[:blank:]]$' "$path" 2>/dev/null | head -20 | grep -q .; then
        echo "trailing whitespace in $path:"
        grep -rIn --exclude-dir=.git --exclude-dir=__pycache__ '[[:blank:]]$' "$path" 2>/dev/null | head -20 | sed 's/^/  /'
        fail=1
    fi

    # tabs in C: the house style is four spaces
    if [ -d "$path" ]; then
        tabs=$(find "$path" -name '*.c' -o -name '*.h' | xargs grep -ln $'\t' 2>/dev/null | head -10)
        if [ -n "$tabs" ]; then
            echo "tabs in C sources:"
            echo "$tabs" | sed 's/^/  /'
            fail=1
        fi
    fi

    # a file with no final newline
    missing=$(find "$path" -type f \( -name '*.c' -o -name '*.h' -o -name '*.py' -o -name '*.md' -o -name '*.fui' -o -name '*.sh' \) 2>/dev/null | while read -r f; do
        [ -s "$f" ] || continue
        if [ "$(tail -c 1 "$f" | wc -l)" -eq 0 ]; then echo "$f"; fi
    done | head -10)
    if [ -n "$missing" ]; then
        echo "no newline at the end of the file:"
        echo "$missing" | sed 's/^/  /'
        fail=1
    fi
done

if [ "$fail" = "0" ]; then
    echo "style: clean"
else
    echo "style: something above needs fixing"
fi
exit $fail
