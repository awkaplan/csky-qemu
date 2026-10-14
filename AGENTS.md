# Notes for AI coding agents

Read [CONTRIBUTING.md](CONTRIBUTING.md) first; it is the full guide. These are the points that are
easiest to get wrong.

## What this repository is

A C-SKY ABIv2 (CK803) CPU target for QEMU, shipped as files that `install.sh` copies into a QEMU
checkout at the commit in `QEMU_COMMIT`. It is not a QEMU tree: nothing here builds on its own,
and paths such as `target/csky/cpu.c` mean the same file in this repository and in the checkout.
The port's history and every API change it made are in [docs/PORT-NOTES.md](docs/PORT-NOTES.md).

## Rules

- Do not modify QEMU core files. The only allowed edits outside the C-SKY files are the build
  registrations in `integration/top-level.diff`.
- Edit files in this repository, then reinstall (`./install.sh <qemu>`). Edits made directly in a
  QEMU checkout are overwritten by the next install and are not part of this repository.
- `hw/csky/Kconfig` and `hw/csky/meson.build` are overwritten by
  [dm32uv-qemu](https://github.com/awkaplan/dm32uv-qemu) when it is installed; mirror any change to
  them there. Its board code also depends on `include/hw/csky/*.h` and `hw/csky/boot.c`.
- The target is little-endian, ABIv2 only and system emulation only. `CONFIG_USER_ONLY`,
  `TARGET_WORDS_BIGENDIAN` and ABIv1 code paths have been removed on purpose; do not add them back.
- Keep the first-line `SPDX-License-Identifier` and the license/provenance paragraph of every file
  unchanged. New files get `/* SPDX-License-Identifier: LGPL-2.1-or-later */` and a copyright line.
- Comments: QEMU block style, present tense, no narration of how something was found. Port-specific
  explanations start with `Port note:` and stay short.

## Checking a change

Run all of these after reinstalling and rebuilding (`ninja -C <qemu>/build qemu-system-cskyv2`):

| Command | Passes when |
|---|---|
| `ninja -C <qemu>/build qemu-system-cskyv2` | no warnings (they are errors by default) |
| `tests/run-smoke.sh <qemu>/build/qemu-system-cskyv2` | prints `csky-qemu smoke test: PASS`, exits 0 |
| `tests/checkpatch.sh <qemu>` | prints `checkpatch: clean` |
| `tests/objhash.py <qemu>` before and after | identical output, for changes meant to be behaviour-neutral |

The smoke test covers basic execution only. If you change instruction translation, helpers, the
MMU, interrupts or devices, say that the DM-32UV firmware boot in dm32uv-qemu still needs to be run,
unless you ran it yourself.
