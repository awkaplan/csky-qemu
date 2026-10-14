# Porting notes: C-SKY target, old fork to modern QEMU

This document covers the port of the C-SKY (CK803, ABIv2) CPU target and the generic C-SKY
devices from the old `c-sky/qemu` fork (base commit `b517e1dc`, ~2019, QEMU 3.0 era) to current
QEMU `master` (11.1.50-dev, October 2026, Meson-based). The port is tested against the QEMU
commit recorded in [`QEMU_COMMIT`](../QEMU_COMMIT). The changes are listed here with the reasons
for each decision, as a reference for maintaining the port against future QEMU changes and for
anyone porting a different old QEMU target.

Source comments starting with `Port note:` mark the places in the code where these changes live.
The DM-32UV radio is the board this port was done for; its board model is in the separate
dm32uv-qemu repository (https://github.com/awkaplan/dm32uv-qemu), which is where the DM-32UV
firmware boot regression lives.

## Contents

- [CPU-target registration model](#cpu-target-registration-model)
- [TCGCPUOps (`accel/tcg/cpu-ops.h`)](#tcgcpuops-acceltcgcpu-opsh)
- [TCG op-generation API](#tcg-op-generation-api)
- [gdbstub](#gdbstub)
- [QAPI / machine registration](#qapi--machine-registration)
- [Header relocations (non-exhaustive, but the pattern)](#header-relocations-non-exhaustive-but-the-pattern)
- [Device/QOM API changes (affects every `hw/*.c` file touched)](#deviceqom-api-changes-affects-every-hwc-file-touched)
- [Not ported](#not-ported)
- [Runtime bugs (not compile-time)](#runtime-bugs-not-compile-time)
- [linux-user (`qemu-cskyv2`)](#linux-user-qemu-cskyv2)
- [Testing](#testing)

## CPU-target registration model

`target/csky/cpu.h`, `cpu.c`, `cpu-qom.h`, `cpu-param.h` are all new or rewritten:

- `CPUArchState` is no longer a `#define` alias (`#define CPUArchState struct CPUCSKYState`). The
  struct's real tag must be `CPUArchState` itself (`typedef struct CPUArchState {...} CPUCSKYState;`),
  with `CSKYCPU` separately aliased to `struct ArchCPU` via
  `OBJECT_DECLARE_CPU_TYPE(CSKYCPU, CSKYCPUClass, CSKY_CPU)` in `cpu-qom.h`. Getting this backwards
  compiles far enough to produce "incompatible pointer type" errors at every `cpu_env(cs)` call
  site.
- `TARGET_LONG_BITS`, `TARGET_PAGE_BITS`, `TARGET_PHYS_ADDR_SPACE_BITS` and
  `TARGET_VIRT_ADDR_SPACE_BITS` moved out of `cpu.h` into a new required `cpu-param.h`.
- `NB_MMU_MODES`, `target_ulong` (replaced by `vaddr`), `TARGET_WORDS_BIGENDIAN`,
  `TCG_GUEST_DEFAULT_MO` and `ALIGNED_ONLY` are all `#pragma GCC poison`ed for any target that does
  not define them. Even a dead `#ifdef TARGET_WORDS_BIGENDIAN` guarding unreachable code is a hard
  compile error, not a warning. This is pervasive: about 15 separate `#ifdef` /
  `#ifndef TARGET_WORDS_BIGENDIAN` sites across `translate_v2.c`, `op_fpuv3.c` and `hw/csky/boot.c`
  needed stripping, keeping only the live little-endian branch.
- The `CPU_COMMON` macro (which pasted TLB-cache fields into `CPUArchState`) is gone. That state
  lives on `CPUState` now. The macro was simply deleted, since nothing referenced the pasted fields
  directly.
- `CSKYCPUClass`'s `parent_reset` / `void (*reset)(CPUState *)` became `ResettablePhases
  parent_phases` plus `resettable_class_set_parent_phases()`. `CPUClass::reset` is gone and is
  replaced by `csky_cpu_reset_hold(Object *, ResetType)`.

## TCGCPUOps (`accel/tcg/cpu-ops.h`)

Replaces most of what used to be plain `CPUClass` fields:

- `tcg_initialize` becomes `.initialize`.
- `gen_intermediate_code()` becomes `.translate_code(cpu, tb, *max_insns, pc, host_pc)`.
- A new mandatory `get_tb_cpu_state(CPUState *) -> TCGTBCPUState{pc, flags}` hook replaces the free
  function `cpu_get_tb_cpu_state()`.
- `tlb_fill` now returns `bool` and takes `MMUAccessType`, `bool probe` and `uintptr_t retaddr`
  (it was void with no probe argument).
- `do_unaligned_access` moved here too. It is now `G_NORETURN`, with an explicit contract that it
  must exit by raising an exception. The old fork's version returned normally when
  `UNALIGNED_ACCESS` was a set feature bit (transparent unaligned-access emulation), and **modern
  TCG no longer supports that at all**. This port always raises, which matches the only path the
  DM-32UV can reach, since nothing sets that feature bit. A future port that wants real
  unaligned-access-enabled CSKY cores needs `MO_UNALN` on every affected `tcg_gen_qemu_ld/st`
  instead. That is a real translator change and has not been done.
- `TCGCPUOps.pointer_wrap` is a new mandatory field (`tcg_exec_realizefn` asserts it is non-null).
  Missing it is a silent-until-runtime abort. `cpu_pointer_wrap_uint32` is the right generic
  32-bit wraparound helper (as in `target/rx`).
- The `TranslatorOps.breakpoint_check` hook is gone entirely. No current target implements it, and
  the generic TB-lookup path now invalidates or intercepts TBs containing breakpoints before they
  would run. `disas_log`'s signature changed from `void(db, cpu)` to `bool(db, cpu, FILE *)`.
- The `cpu->singlestep_enabled` field is gone. It is replaced by `tb_cflags(tb) & CF_SINGLE_STEP`.

## TCG op-generation API

- `tcg_const_i32/i64/tl` and all `tcg_temp_free*` / `tcg_temp_local_new*` are gone. Modern TCG
  never requires manual temp freeing, and `tcg_constant_i32/i64/tl` (const temps) replace the
  const-makers. About 400 call sites across `translate_v2.c`, `op_helper_v2.c`, `op_fpuv3.c` and
  `translate.h` were affected. `translator_loop_temp_check()` is also gone.
- The legacy size-suffixed `tcg_gen_qemu_ld8u/ld16u/ld32u/ld8s/ld16s/st8/st16/st32` are replaced
  by the single `tcg_gen_qemu_ld_tl/st_tl(v, addr, idx, MemOp)`.
- `cpu_lduw_code()` and `cpu_ldl_code()` (direct fetch for translation) are replaced by
  `translator_lduw_end()` / `translator_ldl_end()` (translation-time, replay-safe) for the
  translator's own fetch loop. For the few runtime-helper-side fetches (such as exception-vector
  table reads), use `cpu_ldl_code_mmu(env, addr, MemOpIdx, ra)`, which needs a manually built
  `MemOpIdx` via `make_memop_idx()`. These are genuinely different APIs for translation-time and
  runtime fetches, not just a rename.
- `cpu_{ld,st}{l,w}_data()` becomes `cpu_{ld,st}{l,w}_le_data_ra(env, addr[, val], ra)`, which is
  endianness-explicit and retaddr-explicit.
- `tcg_gen_bswap32_tl` gained a mandatory third `MemOp flags` argument.
  `TCG_BSWAP_IZ | TCG_BSWAP_OZ` is the correct value for a plain 32-bit swap.
- `tcg_gen_insn_start()` gained two trailing `uint64_t a1, a2` aux-data arguments. CSKY passes 0, 0,
  since it has no per-instruction state to carry.
- The `TCGMemOp` typedef was renamed `MemOp`.
- The target's own `translate.c` must directly `#include "exec/helper-info.c.inc"`, wrapped in
  `#define HELPER_H "helper.h"` ... `#undef HELPER_H`. This defines the `TCGHelperInfo
  helper_info_NAME` structs. Without it, about 265 "undefined symbol `_helper_info_*`" errors
  appear. These are **link** errors, not compile errors, so the build looks clean until the final
  link.

## gdbstub

The gdbstub was rewritten substantially:

- `gdb_get_reg32/64()` and related functions now *append* to a growing `GByteArray *` instead of
  writing at a caller-chosen byte offset in a fixed buffer. This broke the old pattern of four
  separate calls at `mem_buf + 0x4*k` offsets for 128-bit vector registers. Those registers are now
  written as four sequential appending calls, with the same net bytes and layout.
- `csky_cpu_gdb_read_register`'s `uint8_t *mem_buf` parameter became `GByteArray *mem_buf`.
  `write_register` keeps a plain `uint8_t *`.
- `CPUClass::gdb_num_core_regs` must be left unset (0) whenever `gdb_core_xml_file` is set.
  `gdb_init_cpu()` now asserts `!gdb_num_core_regs` in that case. The old fork set both, and this is
  a real runtime abort, not just a compile issue.
- `gdb_arch_name` returns `const gchar *` (a string literal) instead of `gchar *`. The old code
  `g_strdup`'d the name and leaked it on every call.
- The `gdb-xml/` directory moved to `gdbstub/gdb-xml/`.
- Build-structure change: `gdbstub.c` had to move from `target_common_system_arch` to
  `target_arch` in `target/csky/meson.build`. `target_common_system_arch` compiles **without**
  `-DCOMPILING_PER_TARGET`, so `qemu/osdep.h` takes the `#include "exec/poison.h"` branch instead of
  `#include CONFIG_TARGET`, which poisons `TARGET_LONG_BITS` for that compilation unit. `avr`'s and
  `rx`'s `gdbstub.c` do not hit this because they include only `cpu.h` and `gdbstub/helpers.h`.
  CSKY's `gdbstub.c` needs `translate.h` (for the MMU-switching function pointers), which pulls in
  `accel/tcg/cpu-ldst.h` and then `exec/target_long.h`, and that hits the poison.
- `csky_tcip_v1.c` separately needs `specific_ss` (not `system_ss`) in `hw/intc/meson.build`,
  because it directly uses `CPUCSKYState` and `CSKYCPU`. A generic `hw/intc/` file is not normally
  expected to have this target dependency, but this one does. See the comment left in
  `hw/intc/meson.build`.

## QAPI / machine registration

- `qapi/machine.json`'s `SysEmuTarget` enum needs `'csky'` added. Without it, `target-info-stub.c`
  fails with `undeclared identifier SYS_EMU_TARGET_CSKY`.
- `type_register()` (the non-`_static` dynamic-`TypeInfo` variant) no longer exists anywhere in
  modern QEMU. `type_register_static()` is used even for the per-CPU-model pattern of building a
  `TypeInfo` with a heap-allocated name on the stack, as in `arm_cpu_register()` in
  `target/arm/cpu.c`.

## Header relocations (non-exhaustive, but the pattern)

- `hw/sysbus.h` to `hw/core/sysbus.h`
- `hw/irq.h` to `hw/core/irq.h`
- `hw/hw.h`, `hw/devices.h` and `qemu-common.h` are gone. They were dead includes and were removed.
- `hw/ptimer.h` to `hw/core/ptimer.h`
- `hw/loader.h` to `hw/core/loader.h`
- `hw/boards.h` to `hw/core/boards.h`
- `sysemu/*.h` to `system/*.h`: `kvm.h`, `numa.h`, `device_tree.h`, `qtest.h`, and `sysemu.h`
  becomes `system.h`.
- `exec/address-spaces.h` to `system/address-spaces.h`
- `exec/memory.h` to `system/memory.h`
- `exec/semihost.h` to `semihosting/semihost.h`. The whole semihosting syscall API was redesigned
  (see [Not ported](#not-ported), item 5).
- `exec/cpu_ldst.h` to `accel/tcg/cpu-ldst.h`, plus `cpu-ldst-common.h` for the `_mmu` variants.
- `exec/exec-all.h` is split across `exec/cputlb.h`, `accel/tcg/cpu-loop.h` and
  `exec/translation-block.h`, depending on what is used.
- `exec/gen-icount.h` is gone. icount is now internal to the generic translator.
- `trace-tcg.h` is gone (a dead include).
- `tcg-op.h` to `tcg/tcg-op.h`

## Device/QOM API changes (affects every `hw/*.c` file touched)

- `class_init(ObjectClass *, void *data)` becomes `(..., const void *data)`.
- `static Property foo[] = {...DEFINE_PROP_END_OF_LIST()}` becomes `static const Property foo[] = {...}`
  (no sentinel), with `device_class_set_props(dc, foo)` instead of `dc->props = foo`.
- `dc->reset = fn` becomes `device_class_set_legacy_reset(dc, fn)`.
- `.fields = (VMStateField[])` becomes `(const VMStateField[])`.
- `DEVICE_CATEGORY_CSKY` does not exist upstream. `DEVICE_CATEGORY_MISC` is substituted.
- `qdev_create()` + `qdev_prop_set_chr()` + `qdev_init_nofail()` becomes `qdev_new()` +
  `qdev_prop_set_chr()` + `sysbus_realize_and_unref()`.
- `CharBackend` becomes `CharFrontend`. The chardev event callback changes from
  `void(void *, int)` to `void(void *, QEMUChrEvent)`.
- `object_property_set_bool(obj, value, name, errp)` becomes `(obj, name, value, errp)`. **The
  argument order is swapped**, which is easy to miss.
- `memory_region_allocate_system_memory()` is gone. Use `memory_region_init_ram(mr, owner, name,
  size, errp)`.
- `address_space_stl_notdirty()` is gone. Use plain `address_space_stl()`.
- `cpu_physical_memory_write()` is gone. Use `address_space_write(as, addr, attrs, buf, len)`.
- `load_elf()` gained two new parameters: an `elf_note_fn` callback inserted second, and a trailing
  `uint32_t *pflags`. It goes from 10 to 12 arguments.
- `load_image_targphys()` gained a trailing `Error **errp`.
- `qemu_fprintf()` needs `qemu/qemu-print.h`.
- `CPUClass::memory_rw_debug`'s `len` is now `size_t`, not `int`.

## Not ported

These stem from the old fork patching **core** QEMU files (`vl.c`, `qemu/log.h`, `cputlb.c`,
`gdbstub.c`) to add project-specific globals and options. Porting them would mean patching QEMU
core files again, which an overlay avoids, so each item below was deliberately scoped out:

1. **Hardware JTAG-style trace-port subsystem** (`csky-trace.c`, `tracestub.c`, the `-csky-trace`
   and `-tfilter` options, and a `CPU_TB_TRACE` log-mask bit patched into core `qemu/log.h`). It is
   stubbed as always-disabled (`csky-trace-stub.h`). Every `gen_mem_trace()` and `gen_tb_trace()`
   call site already branches on this, so behaviour is unaffected compared with running the old
   fork without those flags, which the project never passed.
2. **`-cpu-prop` / `-csky-extend` CLI options** (pctrace, elrw, mem_prot, full_mmu,
   unaligned_access, vdsp, cpu_freq, exit_addr, jcount, denormal, hbreak, mmu_default, tb_trace),
   registered via `qemu_add_opts()` in a patched `vl.c`. `csky_cpu_handle_opts()` is reduced to
   `env->hbreak = true` only. The DM-32UV board model needs none of these. It sets its one needed
   feature bit directly in board code.
3. **`old_param` global** (ATAG versus modern Linux boot-parameter selection) and **`is_gdbserver_start`
   global** (a "debugger attached" flag patched into `gdbstub.c`, used by `bkpt16`'s trap-path
   choice). Both default to their modern or no-debugger equivalents. Neither is reachable from a raw
   flash-image boot path, which is not Linux.
4. **CSKY disassembler** (`disas/csky.c`, 2543 lines, derived from binutils). Not ported. It depended
   on a since-removed `disas/bfd.h` and on the whole `bfd_arch_csky` / `bfd_mach_csky_v2` enum space,
   so it would need a real rewrite against the modern `disassemble_info` shape. `info->print_insn`
   is left unset. With it unset, `-d in_asm` safely prints raw hex instead of mnemonics, **provided
   `info->endian` is set**. `disas_initialize_debug_target()` asserts
   `endian != BFD_ENDIAN_UNKNOWN` unconditionally, even when `print_insn` is NULL.
5. **GDB/ARM-style semihosting syscalls** (`csky-semi.c`, 415 lines). The old `softmmu-semi.h`
   `GET_ARG` / `SET_ARG` macro API is gone, replaced by `semihosting/syscalls.h`, `guestfd.h` and
   `uaccess.h`, which are structurally different. The file is reduced to a minimal stub that handles
   semihosting-exit and logs everything else as unimplemented. It is reachable only if a guest
   deliberately emits the CSKY semihosting magic breakpoint sequence **and** `-semihosting` is
   passed.
6. **`hw/timer/csky_mptimer.c`** (the CP15 MPTimer coprocessor device). Not ported. The `mtcr` and
   `mfcr` handlers in `op_helper_v2.c` reference `env->mptimerdev` and `csky_mptimer_get/set` by
   type, so a struct-only header with no-op inline stubs satisfies the link. Nothing in this port
   sets `env->mptimerdev` to a non-NULL value, so these paths are unreachable in practice.
7. **`hw/timer/csky_coret.c` and `csky_timer.c`** (generic CK803 system and APB timers). Not ported.
   Their instantiation was removed from `smartl.c` rather than stubbed, so `smartl.c` reflects what
   is actually present. The DM-32UV board model has its own `dm32_timer.c` instead.
8. **`hw/misc/csky_exit.c` and `csky_memlog.c`** (debug-only devices). Handled the same way: removed
   from `smartl.c`.

## Runtime bugs (not compile-time)

- The `csky_cpus[]` CPU-model table had a `{ .name = NULL }` sentinel entry, a convention from the
  old fork for a different loop style. A straightforward
  `for (i = 0; i < ARRAY_SIZE(...); i++)` loop tries to register it as a real CPU type, which aborts
  immediately with `Registering '(null)-csky-cpu' with illegal type name`. Fixed by removing the
  sentinel.
- `TCGCPUOps.pointer_wrap` left unset causes an immediate `assert(tcg_ops->pointer_wrap)` abort in
  `tcg_exec_realizefn`. Fixed by setting it to `cpu_pointer_wrap_uint32`.

## linux-user (`qemu-cskyv2`)

Not evaluated. This port is softmmu-only (`configs/targets/cskyv2-softmmu.mak`), and `linux-user/`
has never been built or touched. The old fork's two linux-user modernization patches are untested
against modern QEMU's linux-user and may or may not still be relevant. They rename the internal
`gettid()` to `sys_gettid()` for glibc 2.30 and later, and guard the `SIOCGSTAMP` / `SIOCGSTAMPNS`
ioctls for modern kernel headers.

## Testing

- `tests/run-smoke.sh` boots a small CK803 program on `-M smartl` and checks its UART output. It
  needs no firmware.
- `tests/checkpatch.sh` runs QEMU's style checker with an explicit allowlist in
  `tests/checkpatch-allow.txt`.
- The DM-32UV firmware boot is the much broader regression. It lives with dm32uv-qemu
  (https://github.com/awkaplan/dm32uv-qemu), not in this repository.
