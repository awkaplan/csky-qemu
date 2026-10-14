#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Usage: tests/run-smoke.sh /path/to/qemu-system-cskyv2
# Boots tests/smoke/hello.elf on the smartl board and checks for the PASS line.
# Exit 0 only if PASS was printed; the program halts by design, so timeout's
# status 124 is expected and not treated as an error.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 /path/to/qemu-system-cskyv2" >&2
    exit 2
fi

QEMU="$1"
DIR="$(cd "$(dirname "$0")" && pwd)"
ELF="$DIR/smoke/hello.elf"
PASS="csky-qemu smoke test: PASS"

rc=0
out="$(timeout 10 "$QEMU" -M smartl -nographic -monitor none \
    -serial stdio -kernel "$ELF" < /dev/null)" || rc=$?

echo "$out"
if grep -qF "$PASS" <<< "$out"; then
    echo "smoke test: OK (qemu exit status $rc, 124 = timeout, expected)"
    exit 0
fi
echo "smoke test: FAILED - no PASS line from $QEMU (qemu exit status $rc)" >&2
exit 1
