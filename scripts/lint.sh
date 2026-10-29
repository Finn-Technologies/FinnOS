#!/bin/sh
# The tree's style and single-source checks. Run from the root.
set -eu

git diff --check

# The token header is generated from ui/_tokens.fui, and the checked-in copy
# must be what the generator writes today.
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
python3 tools/tokens.py ui/_tokens.fui "$tmp"
cmp -s "$tmp" src/ui/generated_tokens.hpp || {
    echo "style: src/ui/generated_tokens.hpp is stale; run make tokens" >&2
    exit 1
}

# An app is a program with a toplevel. The compositor must never learn an app
# id, or the launch model has slipped back into the old switchboard.
if grep -R -n -E '"(settings|files|store|hello)"' src/comp --include='*.cpp' \
        --include='*.hpp'; then
    echo "style: app id found in src/comp; the compositor must not know apps" >&2
    exit 1
fi

python3 -m compileall -q tools tests
echo "style: clean"
