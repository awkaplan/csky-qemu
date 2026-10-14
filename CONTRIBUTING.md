# Contributing

Issues and pull requests are welcome. This page covers how the repository is put together, how to
test a change, and the conventions the code follows.

## How the overlay works

The repository holds only C-SKY files. `install.sh` copies the `target/`, `hw/`, `include/`,
`configs/` and `gdbstub/` trees into a QEMU checkout at the same paths, and applies
`integration/top-level.diff`, which adds one or two lines each to QEMU's build files (`Kconfig`,
`meson.build`, `qapi/machine.json`).

The rule that keeps this workable: **no changes to QEMU core files.** Anything that would need one
(the old fork patched `vl.c`, `qemu/log.h`, `cputlb.c` and `gdbstub.c`) is stubbed or left out, and
recorded in [docs/PORT-NOTES.md](docs/PORT-NOTES.md#not-ported). The only edits to QEMU's own files
are the build registrations in `integration/top-level.diff`.

[dm32uv-qemu](https://github.com/awkaplan/dm32uv-qemu) installs on top of this repository, so
changes here can break it:
- it overwrites `hw/csky/Kconfig` and `hw/csky/meson.build` with its own copies, which add the
  `dm32` machine; a change to either file here needs the same change there;
- it uses `include/hw/csky/*.h`, `hw/csky/boot.c` (`csky_load_kernel`), the UART and the interrupt
  controller;
- its own `integration/top-level.diff` also patches `hw/intc/Kconfig` and `hw/intc/meson.build`.

## Building and testing

Set up a QEMU checkout at the commit in `QEMU_COMMIT` and build it as the
[README](README.md#quick-start) shows. After a change, reinstall and rebuild:

```sh
./install.sh ../qemu && ninja -C ../qemu/build qemu-system-cskyv2
tests/run-smoke.sh ../qemu/build/qemu-system-cskyv2
tests/checkpatch.sh ../qemu
```

QEMU builds with warnings as errors by default; keep it that way. The smoke test covers basic
execution only. For any change that can affect execution (the translator, helpers, MMU, interrupt
controller, timers or UART), also run the DM-32UV firmware boot from dm32uv-qemu, which is the real
regression test.

For a change that should not alter behaviour (comments, formatting, removing dead code), prove it
with `tests/objhash.py`: run it on the built checkout before and after the change and diff the
output. Identical hashes mean identical machine code.

## Code style

- QEMU's [coding style](https://www.qemu.org/docs/master/devel/style.html): 4-space indents, braces
  on every `if`, `/* */` comments with the opening and closing markers on their own lines, 80
  columns (90 at most). `tests/checkpatch.sh` runs QEMU's checker; a finding that has to stay goes in
  `tests/checkpatch-allow.txt` with a comment saying why.
- Where the code differs from c-sky/qemu because of the port, say so in a comment that starts with
  `Port note:`, in a few lines, and put any longer history in `docs/PORT-NOTES.md`.
- Comments describe the code as it is, in present tense. Leave out how something was found.
- Files keep QEMU's layout. A new file goes at the path it will have inside QEMU.

## Licensing

Every source file starts with an `SPDX-License-Identifier` line; see [LICENSE.md](LICENSE.md).
New files are `LGPL-2.1-or-later` and carry a copyright line. Files from c-sky/qemu keep their
upstream notice. When you change one substantially, the modification notice under its title
should still describe what has changed, as section 2(a) of the GPL and LGPL requires.

## Moving to a newer QEMU

1. Check out the new QEMU commit, run `install.sh`, and build.
2. If `integration/top-level.diff` no longer applies, add its lines by hand, then regenerate it
   with `git diff` in the QEMU checkout, limited to the files it touches.
3. Fix compile errors against the changed APIs. Record each kind of change in
   `docs/PORT-NOTES.md`, and add a `Port note:` comment where the reason is not obvious.
4. Run the tests above, including the dm32uv-qemu firmware boot, then update `QEMU_COMMIT`.

## Commits

Use QEMU-style subjects: `target/csky: ...`, `hw/csky: ...`, `tests: ...`, `docs: ...`, with a
body that says what changed and why.
