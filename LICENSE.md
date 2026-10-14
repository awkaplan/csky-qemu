# License

Copyright 2026 Alex Kaplan (https://github.com/awkaplan/csky-qemu), for the changes and new files
described below. The C-SKY target this repository ports is the work of C-SKY Microsystems.

This repository is free software under the GNU General Public License and the GNU Lesser General
Public License. Every source file states its own terms in an `SPDX-License-Identifier` line at the
top. The full license texts are in [`COPYING`](COPYING) (GPL, version 2) and
[`COPYING.LIB`](COPYING.LIB) (LGPL, version 2.1), copied unchanged from QEMU.

## Which terms apply to which files

| Files | License | Why |
|---|---|---|
| Files taken from [c-sky/qemu](https://github.com/c-sky/qemu) (commit `b517e1dc`) that carry the upstream LGPL notice: most of `target/csky/`, `hw/`, `include/hw/char/`, `include/hw/csky/` | `LGPL-2.1-or-later` | The upstream notice, kept unchanged, offers the LGPL "version 2 ... or (at your option) any later version". The modified files are distributed under version 2.1 or later, as that notice permits and as QEMU requires of its own LGPL files. |
| `target/csky/op_fpuv3.c`, `include/hw/timer/csky_mptimer.h` | `GPL-2.0-or-later` | The upstream files carry no license notice. QEMU's `LICENSE` puts such files under the GPL, version 2 or later. |
| `gdbstub/gdb-xml/csky-abiv2-softmmu-core.xml` | FSF all-permissive notice | Kept as upstream. |
| Files new in this repository: stubs that replace upstream files (`target/csky/csky-semi.c`, `target/csky/disas.c`, `target/csky/csky-trace-stub.h`), build files (`Kconfig`, `meson.build`, `configs/`), `install.sh` and the documentation | `LGPL-2.1-or-later` | The same terms as the upstream files they extend. |
| `integration/top-level.diff` | The license of each QEMU file it changes | It is a patch to QEMU's own build files. |

Builds of this overlay link into QEMU, and QEMU as a whole is licensed under the GPL, version 2. A
`qemu-system-cskyv2` binary built with this overlay is therefore distributed under GPL-2.0.

## Modifications

Every file taken from c-sky/qemu was modified in October 2026 to port it to current QEMU, and its
header says so, as section 2(a) of both licenses requires. [`docs/PORT-NOTES.md`](docs/PORT-NOTES.md)
describes the changes, and the git history holds the exact diffs.

## What this license does not cover

1. **QEMU.** This repository does not include or redistribute QEMU. You need your own QEMU
   checkout, under QEMU's own terms, to build it.
2. **Firmware.** No radio or vendor firmware is included. The
   [dm32uv-re](https://github.com/awkaplan/dm32uv-re) project, which uses this target, licenses
   its own material under the PolyForm Noncommercial License 1.0.0. Those terms do not apply
   here: code derived from LGPL and GPL sources cannot take on a noncommercial restriction.
