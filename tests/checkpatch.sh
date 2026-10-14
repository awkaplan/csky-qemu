#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Run QEMU's scripts/checkpatch.pl over every C file in this repository.
#
# Usage: tests/checkpatch.sh /path/to/qemu
#
# Fails if checkpatch reports anything not listed in tests/checkpatch-allow.txt. Each line there
# is "<path>: <exact checkpatch message>" ("*" as the path matches any file); keep a comment
# above each entry saying why it stays.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU="$(cd "${1:?usage: tests/checkpatch.sh /path/to/qemu}" && pwd)"
CHECKPATCH="$QEMU/scripts/checkpatch.pl"
ALLOW="$ROOT/tests/checkpatch-allow.txt"

[[ -x "$CHECKPATCH" ]] || { echo "error: $CHECKPATCH not found" >&2; exit 1; }

found="$(mktemp)"
trap 'rm -f "$found"' EXIT
cd "$ROOT"
# checkpatch exits 0 (clean) or 1 (findings). Anything else, or output that is not a finding or
# the "total:" line, means it did not run properly (e.g. missing Perl modules): fail, never pass.
while read -r file; do
    rc=0
    out="$("$CHECKPATCH" --no-tree --terse -f "$file" 2>&1)" || rc=$?
    other="$(grep -vE '^[^:]+:[0-9]+: (ERROR|WARNING|CHECK): |^total: ' <<< "$out" | grep . || true)"
    if (( rc > 1 )) || [[ -n "$other" ]]; then
        echo "error: checkpatch failed on $file (exit $rc):" >&2
        echo "$out" >&2
        exit 1
    fi
    grep -E ': (ERROR|WARNING|CHECK): ' <<< "$out" || true
done < <(find target hw include tests -name '*.[ch]' | sort) > "$found"

# --terse lines look like "path:line: ERROR: message"; drop the line number, then drop every
# line that matches an allowlist entry exactly or, for "*: message" entries, by message alone.
exact="$(grep -v '^#' "$ALLOW" | grep -v '^\*: ' | grep . || true)"
anypath="$(grep '^\*: ' "$ALLOW" | sed 's/^\*: //' || true)"
unexpected="$(sed -E 's/^([^:]+):[0-9]+: /\1: /' "$found" \
    | grep -vxF -e "${exact:-\x00}" \
    | awk -v pats="$anypath" 'BEGIN { n = split(pats, p, "\n") }
          { msg = $0; sub(/^[^:]+: /, "", msg); for (i = 1; i <= n; i++) if (msg == p[i]) next; print }' \
    || true)"
if [[ -n "$unexpected" ]]; then
    echo "checkpatch findings:"
    echo "$unexpected"
    exit 1
fi
echo "checkpatch: clean ($(wc -l < "$found" | tr -d ' ') allowed findings)"
