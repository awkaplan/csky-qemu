# csky-qemu

[![build](https://github.com/awkaplan/csky-qemu/actions/workflows/build.yml/badge.svg)](https://github.com/awkaplan/csky-qemu/actions/workflows/build.yml)

A C-SKY ABIv2 (CK803) CPU target for current QEMU, packaged as a **source-only overlay**: a set of
files and one small patch that you install into a QEMU checkout, not a fork of QEMU.

Mainline QEMU has never had a C-SKY target. The only implementation was in
[c-sky/qemu](https://github.com/c-sky/qemu), against QEMU 3.x-era internals from 2019. This
repository ports that target to current QEMU: CPU and TCG registration, the TCG op API, gdbstub,
memory and device APIs all changed in between. [docs/PORT-NOTES.md](docs/PORT-NOTES.md) lists what
changed and why.

It provides the CPU, a minimal test board (`-M smartl`) and the generic C-SKY devices that board
uses (a UART and an interrupt controller). The board model it was written for, the Baofeng DM-32UV
handheld radio, is a separate repository built on top of this one:
[dm32uv-qemu](https://github.com/awkaplan/dm32uv-qemu).

## Status

Works:
- ABIv2 instruction translation, including the DSP and FPUv3 extensions, with the CK803 CPU
  models (`-cpu help` lists them).
- System emulation (`qemu-system-cskyv2`) with the software MMU, interrupts and exceptions.
- The gdbstub (`-s`), with C-SKY register descriptions for GDB.
- Booting the DM-32UV's stock firmware to its standby screen under dm32uv-qemu, which is the main
  test of this target.

Not ported (each is stubbed so the rest builds and runs; see
[PORT-NOTES.md](docs/PORT-NOTES.md#not-ported)):
- The disassembler: `-d in_asm` prints raw bytes, not mnemonics.
- Semihosting, apart from the exit call.
- The hardware trace port and the old fork's extra command-line options (`-cpu-prop`,
  `-csky-extend`). Both needed patches to QEMU core files, which an overlay avoids.
- The MPTimer coprocessor device and the CK803 system timers.
- ABIv1 (CK610-class) CPUs and user-mode emulation (`qemu-cskyv2`).

## Quick start

```sh
git clone https://gitlab.com/qemu-project/qemu.git
git -C qemu checkout "$(cat csky-qemu/QEMU_COMMIT)"   # the commit this overlay is tested against
csky-qemu/install.sh qemu
cd qemu
./configure --target-list=cskyv2-softmmu
ninja -C build qemu-system-cskyv2
./build/qemu-system-cskyv2 -M help    # lists smartl (and dm32, once dm32uv-qemu is installed)
```

`install.sh` copies this repository's `target/`, `hw/`, `include/`, `configs/` and `gdbstub/` trees
into the checkout and applies [`integration/top-level.diff`](integration/top-level.diff), which adds
one or two lines each to QEMU's build files to register the target and devices. It is safe to run
again. On a newer QEMU commit, the diff may need its hunks applied by hand and the C code may need
adjusting to API changes; `QEMU_COMMIT` is the last commit known to work.

QEMU's own build dependencies apply (see the
[QEMU build documentation](https://www.qemu.org/docs/master/devel/build-environment.html)); the
smartl board also needs libfdt.

## Testing

```sh
tests/run-smoke.sh qemu/build/qemu-system-cskyv2   # boots a small CK803 program on -M smartl
tests/checkpatch.sh qemu                           # QEMU's style checker, with an allowlist
```

The smoke test ([`tests/smoke/hello.S`](tests/smoke/hello.S)) runs a loop, a multiply and a memory
round-trip, then prints `csky-qemu smoke test: PASS` on the UART. The prebuilt `hello.elf` is
committed so no C-SKY toolchain is needed; `tests/smoke/build.sh` rebuilds it. CI runs both on
every push ([`.github/workflows/build.yml`](.github/workflows/build.yml)).

The broader regression is booting real firmware, which lives with
[dm32uv-qemu](https://github.com/awkaplan/dm32uv-qemu).

## Repository layout

The source trees mirror QEMU's own layout, so every file lands at the same path in the checkout.

```
target/csky/                 the CPU target
  cpu.c, cpu.h, cpu-qom.h    CPU models, state, reset, QOM registration
  cpu-param.h                address-space and page-size parameters
  translate_v2.c             ABIv2 instruction decoder and TCG translator
  translate.h                translator helpers shared with the MMU code
  op_helper_v2.c, helper.c   runtime helpers: exceptions, interrupts, control registers
  op_fpuv3.c                 FPUv3 helpers
  mmu.c                      software MMU and memory protection
  gdbstub.c                  GDB register access
  disas.c, csky-semi.c,      stubs for what is not ported
  csky-trace-stub.h
hw/csky/                     boot.c (ELF/raw image loader), smartl.c (test board)
hw/intc/csky_tcip_v1.c       C-SKY interrupt controller (TCIP v1)
hw/char/csky_uart.c          DesignWare-style UART
include/hw/                  headers for the above
configs/, gdbstub/gdb-xml/   target registration, default devices, GDB register XML
integration/top-level.diff   the patch to QEMU's build files
install.sh                   installs everything into a QEMU checkout
QEMU_COMMIT                  the QEMU commit this is tested against
tests/                       smoke test and style check
docs/PORT-NOTES.md           what changed from c-sky/qemu, and why
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). AI coding tools should also read [AGENTS.md](AGENTS.md).

## Credits

The C-SKY CPU target and the generic C-SKY devices come from
[c-sky/qemu](https://github.com/c-sky/qemu) by C-SKY Microsystems (Hangzhou C-SKY Microsystems
Co., Ltd.); its C-SKY target commits are authored by Guo Ren. This port starts from commit
[`b517e1dc`](https://github.com/c-sky/qemu/commit/b517e1dc3125a57555d67a8deed9eac7b42288e2)
(2019-06-03). Every file taken from it was modified in October 2026 to port it to current QEMU,
and says so in its header.

## License

GPL and LGPL; each file carries an `SPDX-License-Identifier` line. See [LICENSE.md](LICENSE.md)
for which terms apply to which files, and [COPYING](COPYING) and [COPYING.LIB](COPYING.LIB) for the
license texts.
