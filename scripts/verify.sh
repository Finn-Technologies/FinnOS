#!/bin/bash
# Verify the whole system, end to end.
#
#   scripts/verify.sh
#
# The tests and the style checks here; the build, the install and the
# captures on the dev VM. It is the loop that says "it works" rather than
# "it compiles": the captures are the proof, and they are written to
# /tmp so they can be looked at.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HOST="${FINN_HOST:-finn@10.211.55.6}"
OUT="${FINN_SHOT_DIR:-/tmp}"
fail=0

step() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
ok()   { printf '    ok\n'; }
bad()  { printf '    FAILED\n'; fail=1; }

step "the tests"
if make test >/tmp/finn-verify-tests.log 2>&1; then
    tail -3 /tmp/finn-verify-tests.log | sed 's/^/    /'
    ok
else
    tail -20 /tmp/finn-verify-tests.log | sed 's/^/    /'
    bad
fi

step "the style checks"
if scripts/lint.sh >/tmp/finn-verify-lint.log 2>&1; then
    ok
else
    cat /tmp/finn-verify-lint.log | sed 's/^/    /'
    bad
fi

step "the build on the VM"
if FINN_HOST="$HOST" vm/deploy.sh >/tmp/finn-verify-deploy.log 2>&1; then
    grep -E "desktop live|compositing on|control on" /tmp/finn-verify-deploy.log | sed 's/^/    /'
    ok
else
    tail -30 /tmp/finn-verify-deploy.log | sed 's/^/    /'
    bad
fi

step "the shell, on the machine"
if ssh "$HOST" 'sudo finnctl screen.shell' >/tmp/finn-verify-shell.log 2>&1; then
    sed 's/^/    /' /tmp/finn-verify-shell.log
    ok
else
    bad
fi

step "the captures"
capture() {
    local name="$1"
    if FINN_HOST="$HOST" vm/shot.sh "$OUT/$name.png" 2 >/dev/null 2>&1; then
        printf '    %s\n' "$OUT/$name.png"
    else
        bad
    fi
}

# the desktop
ssh "$HOST" 'sudo finnctl pointer.move x=100 y=100' >/dev/null 2>&1
capture finn-desktop

# the Start Menu, in both appearances
ssh "$HOST" 'sudo finnctl pointer.move x=1196 y=1506; sudo finnctl pointer.click' >/dev/null 2>&1
sleep 1
capture finn-start-menu

ssh "$HOST" 'sudo finnctl appearance.set dark=1' >/dev/null 2>&1
sleep 1
capture finn-start-menu-dark

ssh "$HOST" 'sudo finnctl appearance.set dark=0; sudo finnctl pointer.click' >/dev/null 2>&1
sleep 1

step "the journal"
if ssh "$HOST" 'sudo journalctl -u finn-comp -n 40 --no-pager -o cat' >/tmp/finn-verify-journal.log 2>&1; then
    if grep -qiE "error|failed|segmentation" /tmp/finn-verify-journal.log; then
        grep -iE "error|failed|segmentation" /tmp/finn-verify-journal.log | sed 's/^/    /'
        bad
    else
        echo "    nothing in the journal"
        ok
    fi
fi

printf '\n'
if [ "$fail" = "0" ]; then
    echo "verify: everything passed"
else
    echo "verify: something above did not pass"
fi
exit $fail
