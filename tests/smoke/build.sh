#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Rebuild smoke/hello.elf with the C-SKY ABIv2 toolchain.
# Override the toolchain prefix with CROSS, e.g.
#   CROSS=/opt/csky-elfabiv2-toolchain/bin/csky-abiv2-elf- tests/smoke/build.sh
set -euo pipefail

cd "$(dirname "$0")"
CROSS="${CROSS:-csky-abiv2-elf-}"

"${CROSS}gcc" -mcpu=ck803 -nostdlib -nostartfiles -T hello.ld \
    -o hello.elf hello.S
"${CROSS}strip" hello.elf
echo "built $(pwd)/hello.elf"
