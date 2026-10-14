#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Install the C-SKY (CK803) target into a QEMU source checkout.
#
# Usage: ./install.sh /path/to/qemu
#
# Copies this repository's target/, hw/, include/, configs/ and gdbstub/ trees into the checkout
# (same relative paths), then applies integration/top-level.diff, which registers the target and
# its devices in QEMU's build files. Running it again is safe: the copy overwrites, and the patch
# is skipped when it is already applied.
#
# The overlay is tested against the QEMU commit in QEMU_COMMIT. Other commits usually work, but
# the patch or the C code may need adjusting if QEMU's internal APIs have moved.
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${1:?usage: install.sh /path/to/qemu}"
OVERLAY_DIRS=(target hw include configs gdbstub)

if [[ ! -f "$DEST/VERSION" || ! -d "$DEST/target" ]]; then
    echo "error: $DEST does not look like a QEMU checkout (no VERSION file or target/)" >&2
    exit 1
fi

tested="$(cat "$SRC/QEMU_COMMIT")"
actual="$(git -C "$DEST" rev-parse HEAD 2>/dev/null || echo unknown)"
if [[ "$actual" != "$tested" ]]; then
    echo "note: $DEST is at QEMU commit $actual;"
    echo "      this overlay is tested against $tested (see QEMU_COMMIT)."
fi

echo "copying overlay files..."
for dir in "${OVERLAY_DIRS[@]}"; do
    mkdir -p "$DEST/$dir"
    cp -R "$SRC/$dir/." "$DEST/$dir/"
done

echo "applying integration/top-level.diff..."
patch="$SRC/integration/top-level.diff"
if git -C "$DEST" apply --check "$patch" 2>/dev/null; then
    git -C "$DEST" apply "$patch"
    echo "  applied"
elif git -C "$DEST" apply --reverse --check "$patch" 2>/dev/null; then
    echo "  already applied, skipped"
else
    echo "error: integration/top-level.diff does not apply to this checkout." >&2
    echo "  QEMU's build files have probably changed since $tested." >&2
    echo "  Each hunk adds one or two lines; apply them by hand, or check out $tested." >&2
    exit 1
fi

cat <<DONE

Done. To build:
    cd $DEST
    ./configure --target-list=cskyv2-softmmu
    ninja -C build qemu-system-cskyv2
DONE
